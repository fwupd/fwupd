/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! C bindings for the Rust CAB parser/writer.
//!
//! This module exposes the `fwupd::cab` types as opaque `FuRsCabArchive`
//! handles that can be used from C.  A `GInputStreamReadAt` adapter
//! implements the [`fwupd::cab::ReadAt`] trait by calling
//! `g_seekable_seek` + `g_input_stream_read` via FFI, enabling
//! zero-copy parsing of `GInputStream` sources.
//!
//! # Deferred data and zero-copy spans
//!
//! For uncompressed CAB archives, the Rust parser does not read file data
//! into memory. Instead, it records the byte ranges (spans) within the
//! original source that contain each file's payload. The C caller must
//! check [`fu_rs_cab_file_is_deferred()`] for each file:
//!
//! - **Deferred** (uncompressed): use [`fu_rs_cab_file_nspans()`] and
//!   [`fu_rs_cab_file_span()`] to obtain `(offset, length)` pairs, then
//!   create `FuPartialInputStream` / `FuCompositeInputStream` views
//!   pointing at the original `GInputStream`. The original stream must
//!   remain alive for as long as these views are in use.
//!
//! - **Owned** (compressed): use [`fu_rs_cab_file_data()`] to obtain a
//!   pointer to the decompressed bytes, which are owned by the
//!   `FuRsCabArchive` handle.
//!
//! [`fu_rs_cab_file_size()`] returns the total uncompressed size for
//! either case.

use std::ffi::{CString, c_char, c_void};

use fwupd::cab::*;
use fwupd::firmware::FuFirmwareParseFlags;

use crate::common::*;

// GSeekType constants (note: GLib order differs from POSIX: CUR=0, SET=1, END=2)
const G_SEEK_SET: i32 = 1;
const G_SEEK_END: i32 = 2;

// ---------------------------------------------------------------------------
// GInputStreamReadAt -- wraps a C GInputStream* as a Rust ReadAt
// ---------------------------------------------------------------------------

/// A Rust [`ReadAt`] backed by a C `GInputStream*`.
///
/// Each `read_at()` call performs `g_seekable_seek(SET)` +
/// `g_input_stream_read()` via FFI.  The `GInputStream` is
/// ref-counted: the adapter takes a ref on creation and drops it
/// on `Drop`.
struct GInputStreamReadAt {
    stream: *mut c_void, // GInputStream*, ref-counted
    size: usize,         // cached total size
}

impl GInputStreamReadAt {
    /// Wrap a `GInputStream*`, taking a new reference.
    ///
    /// Returns an error string if the stream is null, not seekable, or
    /// the size cannot be determined.
    ///
    /// # Safety
    ///
    /// `stream` must be a valid, non-null `GInputStream*` that
    /// implements `GSeekable`.
    unsafe fn new(stream: *mut c_void) -> Result<Self, String> {
        if stream.is_null() {
            return Err("stream is NULL".to_string());
        }
        // must be seekable
        if unsafe { g_seekable_can_seek(stream) } == 0 {
            return Err("stream is not seekable".to_string());
        }
        // determine size by seeking to end
        let mut gerr: *mut GError = core::ptr::null_mut();
        if unsafe { g_seekable_seek(stream, 0, G_SEEK_END, core::ptr::null_mut(), &mut gerr) } == 0
        {
            let msg = unsafe { take_gerror_message(gerr) };
            return Err(format!("failed to seek to end: {msg}"));
        }
        let end = unsafe { g_seekable_tell(stream) };
        // seek back to start -- read_at() seeks explicitly before each read
        if unsafe { g_seekable_seek(stream, 0, G_SEEK_SET, core::ptr::null_mut(), &mut gerr) } == 0
        {
            let msg = unsafe { take_gerror_message(gerr) };
            return Err(format!("failed to seek back to start: {msg}"));
        }
        // take a ref
        unsafe { g_object_ref(stream) };
        Ok(Self {
            stream,
            size: end as usize,
        })
    }
}

impl Drop for GInputStreamReadAt {
    fn drop(&mut self) {
        unsafe { g_object_unref(self.stream) };
    }
}

impl ReadAt for GInputStreamReadAt {
    fn size(&self) -> usize {
        self.size
    }

    fn read_at(&self, offset: usize, buf: &mut [u8]) -> Result<usize, CabError> {
        if offset >= self.size || buf.is_empty() {
            return Ok(0);
        }
        let mut gerr: *mut GError = core::ptr::null_mut();
        // seek to offset
        if unsafe {
            g_seekable_seek(
                self.stream,
                offset as i64,
                G_SEEK_SET,
                core::ptr::null_mut(),
                &mut gerr,
            )
        } == 0
        {
            let msg = unsafe { take_gerror_message(gerr) };
            return Err(CabError::Format(format!(
                "seek to offset 0x{offset:x} failed: {msg}"
            )));
        }
        // read
        let n = unsafe {
            g_input_stream_read(
                self.stream,
                buf.as_mut_ptr(),
                buf.len(),
                core::ptr::null_mut(),
                &mut gerr,
            )
        };
        if n < 0 {
            let msg = unsafe { take_gerror_message(gerr) };
            return Err(CabError::Format(format!(
                "read at offset 0x{offset:x} failed: {msg}"
            )));
        }
        Ok(n as usize)
    }
}

// ---------------------------------------------------------------------------
// FuRsCabArchive -- opaque handle for C
// ---------------------------------------------------------------------------

/// Opaque handle wrapping a parsed or constructed CAB archive.
///
/// C code sees this as `typedef struct FuRsCabArchive FuRsCabArchive;`.
pub struct FuRsCabArchive {
    archive: CabArchive,
    /// Cached CString filenames for stable pointer returns.
    name_cache: Vec<CString>,
}

impl FuRsCabArchive {
    fn new(archive: CabArchive) -> Self {
        let name_cache = archive
            .files
            .iter()
            .map(|f| CString::new(f.name.as_str()).unwrap_or_default())
            .collect();
        Self {
            archive,
            name_cache,
        }
    }

    /// Extend the name cache for any newly added files.
    ///
    /// Existing `CString` entries are kept so that pointers previously
    /// returned by `fu_rs_cab_filename()` remain valid.
    fn rebuild_cache(&mut self) {
        while self.name_cache.len() < self.archive.files.len() {
            let f = &self.archive.files[self.name_cache.len()];
            self.name_cache
                .push(CString::new(f.name.as_str()).unwrap_or_default());
        }
    }
}

// ---------------------------------------------------------------------------
// Error mapping
// ---------------------------------------------------------------------------

/// Map a CabError to the corresponding FWUPD_ERROR code.
fn cab_error_code(err: &CabError) -> i32 {
    match err {
        CabError::Format(_) => FWUPD_ERROR_INVALID_DATA,
        CabError::NotSupported(_) => FWUPD_ERROR_NOT_SUPPORTED,
        CabError::Decompression(_) => FWUPD_ERROR_INVALID_DATA,
        CabError::Limit(_) => FWUPD_ERROR_INVALID_DATA,
    }
}

/// Set a GError from a CabError.
///
/// # Safety
///
/// `error` must be a valid `GError**` (or null).
unsafe fn set_gerror_from_cab(error: *mut *mut GError, err: &CabError) {
    let msg = CString::new(err.to_string()).unwrap_or_default();
    unsafe {
        g_set_error_literal(
            error,
            fwupd_error_quark(),
            cab_error_code(err),
            msg.as_ptr(),
        );
    }
}

// ---------------------------------------------------------------------------
// Parse from GInputStream
// ---------------------------------------------------------------------------

/// Parse a CAB archive from a seekable `GInputStream`.
///
/// # Safety
///
/// `stream` must be a valid, non-null, seekable `GInputStream*`.
/// `error` must be a valid `GError**` (or null).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_parse(
    stream: *mut c_void,
    only_basename: i32,
    ignore_checksum: i32,
    error: *mut *mut GError,
) -> *mut FuRsCabArchive {
    let adapter = match unsafe { GInputStreamReadAt::new(stream) } {
        Ok(a) => a,
        Err(reason) => {
            let msg = CString::new(format!("failed to wrap GInputStream as ReadAt: {reason}"))
                .unwrap_or_default();
            unsafe {
                g_set_error_literal(
                    error,
                    fwupd_error_quark(),
                    FWUPD_ERROR_NOT_SUPPORTED,
                    msg.as_ptr(),
                );
            }
            return core::ptr::null_mut();
        }
    };
    let mut flags = FuFirmwareParseFlags::empty();
    if only_basename != 0 {
        flags |= FuFirmwareParseFlags::ONLY_BASENAME;
    }
    if ignore_checksum != 0 {
        flags |= FuFirmwareParseFlags::IGNORE_CHECKSUM;
    }
    match CabArchive::parse(&adapter, flags) {
        Ok(archive) => Box::into_raw(Box::new(FuRsCabArchive::new(archive))),
        Err(e) => {
            unsafe { set_gerror_from_cab(error, &e) };
            core::ptr::null_mut()
        }
    }
}

// ---------------------------------------------------------------------------
// Parse from byte buffer
// ---------------------------------------------------------------------------

/// Parse a CAB archive from a raw byte buffer.
///
/// # Safety
///
/// `buf` must point to `bufsz` valid bytes, or be null with `bufsz` == 0.
/// `error` must be a valid `GError**` (or null).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_parse_bytes(
    buf: *const u8,
    bufsz: usize,
    only_basename: i32,
    ignore_checksum: i32,
    error: *mut *mut GError,
) -> *mut FuRsCabArchive {
    let data = unsafe { buf_to_slice(buf, bufsz) };
    let mut flags = FuFirmwareParseFlags::empty();
    if only_basename != 0 {
        flags |= FuFirmwareParseFlags::ONLY_BASENAME;
    }
    if ignore_checksum != 0 {
        flags |= FuFirmwareParseFlags::IGNORE_CHECKSUM;
    }
    match CabArchive::parse(data, flags) {
        Ok(archive) => Box::into_raw(Box::new(FuRsCabArchive::new(archive))),
        Err(e) => {
            unsafe { set_gerror_from_cab(error, &e) };
            core::ptr::null_mut()
        }
    }
}

// ---------------------------------------------------------------------------
// Validate
// ---------------------------------------------------------------------------

/// Check whether a `GInputStream` starts with the MS Cabinet signature.
///
/// Returns 1 (TRUE) if valid, 0 (FALSE) otherwise.
///
/// # Safety
///
/// `stream` must be a valid, non-null, seekable `GInputStream*`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_validate(stream: *mut c_void) -> i32 {
    let adapter = match unsafe { GInputStreamReadAt::new(stream) } {
        Ok(a) => a,
        Err(_) => return 0,
    };
    if CabArchive::validate(&adapter) { 1 } else { 0 }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/// Return the number of files in the archive.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_nfiles(archive: *const FuRsCabArchive) -> usize {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive.archive.files.len()
}

/// Return a pointer to the NUL-terminated filename of file `idx`.
///
/// The pointer is valid until the archive is freed.
/// Returns null if `idx` is out of bounds.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_filename(
    archive: *const FuRsCabArchive,
    idx: usize,
) -> *const c_char {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return core::ptr::null();
    }
    let archive = unsafe { &*archive };
    match archive.name_cache.get(idx) {
        Some(name) => name.as_ptr(),
        None => core::ptr::null(),
    }
}

/// Return a pointer to the raw file data for file `idx`.
///
/// Sets `*len` to the data length.  The pointer is valid until
/// the archive is freed.  Returns null if `idx` is out of bounds.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_data(
    archive: *const FuRsCabArchive,
    idx: usize,
    len: *mut usize,
) -> *const u8 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        if !len.is_null() {
            unsafe { *len = 0 };
        }
        return core::ptr::null();
    }
    let archive = unsafe { &*archive };
    match archive.archive.files.get(idx) {
        Some(file) => match &file.data {
            CabFileData::Owned(bytes) => {
                if !len.is_null() {
                    unsafe { *len = bytes.len() };
                }
                bytes.as_ptr()
            }
            CabFileData::Deferred(_) => {
                // Deferred data: caller must use fu_rs_cab_file_spans()
                if !len.is_null() {
                    unsafe { *len = 0 };
                }
                core::ptr::null()
            }
        },
        None => {
            if !len.is_null() {
                unsafe { *len = 0 };
            }
            core::ptr::null()
        }
    }
}

/// Return `1` if file `idx` has deferred data (zero-copy spans), `0` if owned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_is_deferred(
    archive: *const FuRsCabArchive,
    idx: usize,
) -> i32 {
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive
        .archive
        .files
        .get(idx)
        .map_or(0, |f| if f.data.is_deferred() { 1 } else { 0 })
}

/// Return the number of spans for deferred file `idx`.
/// Returns 0 if the file has owned data or `idx` is out of range.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_nspans(
    archive: *const FuRsCabArchive,
    idx: usize,
) -> usize {
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive
        .archive
        .files
        .get(idx)
        .and_then(|f| f.data.spans())
        .map_or(0, |s| s.len())
}

/// Return the offset and length of span `span_idx` for deferred file `idx`.
/// Writes to `*offset` and `*length`. Returns 1 on success, 0 on failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_span(
    archive: *const FuRsCabArchive,
    idx: usize,
    span_idx: usize,
    offset: *mut usize,
    length: *mut usize,
) -> i32 {
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    let span = archive
        .archive
        .files
        .get(idx)
        .and_then(|f| f.data.spans())
        .and_then(|s| s.get(span_idx));
    match span {
        Some(s) => {
            if !offset.is_null() {
                unsafe { *offset = s.offset };
            }
            if !length.is_null() {
                unsafe { *length = s.length };
            }
            1
        }
        None => 0,
    }
}

/// Return the total uncompressed size of file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_size(archive: *const FuRsCabArchive, idx: usize) -> usize {
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive.archive.files.get(idx).map_or(0, |f| f.data.len())
}

/// Return the MS-DOS date for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_date(archive: *const FuRsCabArchive, idx: usize) -> u16 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive.archive.files.get(idx).map_or(0, |f| f.date.into())
}

/// Return the MS-DOS time for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_time(archive: *const FuRsCabArchive, idx: usize) -> u16 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive.archive.files.get(idx).map_or(0, |f| f.time.into())
}

/// Return the unpacked year (1980--2107) for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_year(archive: *const FuRsCabArchive, idx: usize) -> u16 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive.archive.files.get(idx).map_or(0, |f| f.date.year())
}

/// Return the unpacked month (1--12) for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_month(archive: *const FuRsCabArchive, idx: usize) -> u8 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive.archive.files.get(idx).map_or(0, |f| f.date.month())
}

/// Return the unpacked day (1--31) for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_day(archive: *const FuRsCabArchive, idx: usize) -> u8 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive.archive.files.get(idx).map_or(0, |f| f.date.day())
}

/// Return the unpacked hour (0--23) for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_hour(archive: *const FuRsCabArchive, idx: usize) -> u8 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive.archive.files.get(idx).map_or(0, |f| f.time.hour())
}

/// Return the unpacked minute (0--59) for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_minute(archive: *const FuRsCabArchive, idx: usize) -> u8 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive
        .archive
        .files
        .get(idx)
        .map_or(0, |f| f.time.minute())
}

/// Return the unpacked second (0--58, 2-second resolution) for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_second(archive: *const FuRsCabArchive, idx: usize) -> u8 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive
        .archive
        .files
        .get(idx)
        .map_or(0, |f| f.time.second())
}

/// Return the MS-DOS attributes for file `idx`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_file_attrs(archive: *const FuRsCabArchive, idx: usize) -> u16 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    archive
        .archive
        .files
        .get(idx)
        .map_or(0, |f| f.attributes.bits())
}

/// Return whether the archive was compressed (MSZIP).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_is_compressed(archive: *const FuRsCabArchive) -> i32 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    if archive.archive.is_compressed { 1 } else { 0 }
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

/// Serialize the archive to a freshly allocated byte buffer.
///
/// The returned pointer is allocated with `g_memdup2` so the
/// C caller can `g_free()` it.  Sets `*out_len` to the length.
/// Returns null on error, setting `*error`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_write(
    archive: *const FuRsCabArchive,
    compressed: i32,
    out_len: *mut usize,
    error: *mut *mut GError,
) -> *mut u8 {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        if !out_len.is_null() {
            unsafe { *out_len = 0 };
        }
        unsafe { set_gerror_from_cab(error, &CabError::Format("archive handle is null".into())) };
        return core::ptr::null_mut();
    }
    let handle = unsafe { &*archive };

    // Build a temporary archive with the desired compression flag
    let mut write_archive = handle.archive.clone();
    write_archive.is_compressed = compressed != 0;

    match write_archive.write() {
        Ok(data) => {
            if !out_len.is_null() {
                unsafe { *out_len = data.len() };
            }
            // Copy into g_malloc'd memory so C can g_free() it
            unsafe { g_memdup2(data.as_ptr(), data.len()) }
        }
        Err(e) => {
            if !out_len.is_null() {
                unsafe { *out_len = 0 };
            }
            unsafe { set_gerror_from_cab(error, &e) };
            core::ptr::null_mut()
        }
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/// Create a new empty archive (for building CABs from C).
#[unsafe(no_mangle)]
pub extern "C" fn fu_rs_cab_new() -> *mut FuRsCabArchive {
    let archive = CabArchive {
        files: Vec::new(),
        is_compressed: false,
    };
    Box::into_raw(Box::new(FuRsCabArchive::new(archive)))
}

/// Add a file to the archive.
///
/// `name` is a NUL-terminated C string.  `data`/`data_len` are copied.
/// Date/time components are packed into MS-DOS format internally.
///
/// # Safety
///
/// `archive` must be a valid `FuRsCabArchive*`.
/// `name` must be a valid NUL-terminated string.
/// `data` must point to `data_len` valid bytes (or be null with len 0).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_add_file(
    archive: *mut FuRsCabArchive,
    name: *const c_char,
    data: *const u8,
    data_len: usize,
    year: u16,
    month: u8,
    day: u8,
    hour: u8,
    minute: u8,
    second: u8,
    attrs: u16,
) {
    debug_assert!(!archive.is_null());
    if archive.is_null() {
        return;
    }
    let handle = unsafe { &mut *archive };
    let name_str = if name.is_null() {
        String::new()
    } else {
        unsafe { std::ffi::CStr::from_ptr(name) }
            .to_string_lossy()
            .into_owned()
    };
    let file_data = unsafe { buf_to_slice(data, data_len) }.to_vec();
    handle.archive.files.push(CabArchiveFile {
        name: name_str,
        data: CabFileData::Owned(file_data),
        date: MsDosDate::from_ymd(year, month, day),
        time: MsDosTime::from_hms(hour, minute, second),
        attributes: MsDosFileAttr::from_bits_truncate(attrs),
    });
    handle.rebuild_cache();
}

/// Free a CAB archive handle.
///
/// Must be called exactly once per handle returned by a constructor
/// or parse function.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_rs_cab_free(archive: *mut FuRsCabArchive) {
    if !archive.is_null() {
        drop(unsafe { Box::from_raw(archive) });
    }
}
