/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! C-compatible FFI wrappers for the CAB archive parser.
//!
//! The archive is created from a a Rust-backed stream, parsed in full at
//! construction time, and then provides lazy indexed access to
//! each contained file. File content is read through the file-entry pointer
//! returned by [`fu_rs_cab_archive_get_file`].

use std::ffi::CStr;
use std::io::Read;
use std::os::raw::c_char;
use std::ptr;

use crate::glib::{GBoolean, GBytes, GError, GString, GFALSE, GTRUE};
use fwupd::cab::{
    Archive, ArchiveBuilder, BuilderError, CabError, CabPath, FileBuilder, FileEntry, HasFiles,
    MsDosDate, MsDosFileAttrs, MsDosTime, ParserFlags,
};
use fwupd::streams::{IntoStream, MemoryInputStream, Stream, StreamImpl};
use fwupd::Bitflags;

/// Opaque handle wrapping a parsed [`Archive`].
pub struct CabArchive(Archive);

/// Create a new CAB archive by parsing the given stream.
///
/// `stream` is cloned from the caller.
/// `offset` is the byte offset within the stream where the CAB data starts.
/// `flags` is a bitmask of parser options.
///
/// Returns a heap-allocated handle on success, or `NULL` on error (sets `*error`).
///
/// # Safety
/// `stream` must be a valid `FuRsStreamImpl *` previously returned by a stream
/// constructor.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_archive_new(
    stream: *const Stream<StreamImpl>,
    offset: usize,
    flags: u32,
    error: *mut *mut GError,
) -> *mut CabArchive {
    let stream = unwrap_or_panic!(unsafe { stream.as_ref() });
    let Ok(parser_flags) = ParserFlags::try_from(flags) else {
        let fe = fwupd::Error::new(
            fwupd::ErrorKind::InvalidData,
            &format!("invalid parser flags: 0x{flags:x}"),
        );
        GError::convert(error, &fe);
        return ptr::null_mut();
    };
    let stream_impl = stream.clone();
    match Archive::new(stream_impl, offset, parser_flags) {
        Ok(archive) => Box::into_raw(Box::new(CabArchive(archive))),
        Err(e) => {
            let kind = match &e {
                CabError::NotSupported(_) => fwupd::ErrorKind::NotSupported,
                CabError::IO(_) => fwupd::ErrorKind::Read,
                _ => fwupd::ErrorKind::InvalidData,
            };
            let fe = fwupd::Error::new(kind, &format!("failed to parse CAB archive: {e}"));
            GError::convert(error, &fe);
            ptr::null_mut()
        }
    }
}

/// Free a CAB archive handle.
///
/// # Safety
/// `archive` must be a valid pointer previously returned by
/// [`fu_rs_cab_archive_new`], or `NULL` (no-op).
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_archive_free(archive: *mut CabArchive) {
    if !archive.is_null() {
        drop(unsafe { Box::from_raw(archive) });
    }
}

/// Return the number of files in the archive.
///
/// # Safety
/// `archive` must be a valid, non-`NULL` pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_archive_get_nfiles(archive: *const CabArchive) -> usize {
    let archive = unwrap_or_panic!(archive.as_ref());
    archive.0.files().len()
}

/// Return the on-disk size of the archive in bytes.
///
/// # Safety
/// `archive` must be a valid, non-`NULL` pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_archive_get_size(archive: *const CabArchive) -> usize {
    let archive = unwrap_or_panic!(archive.as_ref());
    archive.0.size()
}

/// Return `TRUE` if any folder in the archive used MSZIP compression.
///
/// # Safety
/// `archive` must be a valid, non-`NULL` pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_archive_was_compressed(archive: *const CabArchive) -> i32 {
    if archive.is_null() {
        return 0;
    }
    let archive = unsafe { &*archive };
    i32::from(archive.0.was_compressed())
}

/// Return a borrowed pointer to the file entry at index `idx`.
///
/// The returned pointer is **not** separately allocated; it points directly
/// into the archive's file list and is valid for the lifetime of the archive.
/// Do **not** free it.  Returns `NULL` if `idx` is out of bounds.
///
/// # Safety
/// `archive` must be a valid, non-`NULL` pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_archive_get_file(
    archive: *mut CabArchive,
    idx: usize,
) -> *mut FileEntry {
    let archive = unwrap_or_panic!(archive.as_mut());
    let files = archive.0.files_mut();
    if idx >= files.len() {
        return ptr::null_mut();
    }
    &mut files[idx]
}

/// Return the NUL-terminated filename of the file entry.
///
/// The returned pointer is borrowed from the archive and is valid for the
/// lifetime of the archive.
///
/// # Safety
/// `file` must be a valid, non-`NULL` pointer previously returned by
/// [`fu_rs_cab_archive_get_file`].
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_file_entry_get_name(file: *const FileEntry) -> *const c_char {
    let file = unwrap_or_panic!(file.as_ref());
    file.name().path().as_ptr()
}

/// Return the filename of the file entry with backslashes replaced by forward slashes.
///
/// The returned string is heap-allocated; the caller must free it with `g_free`.
/// Returns `NULL` and sets `*error` on failure.
///
/// # Safety
/// `file` must be a valid, non-`NULL` pointer previously returned by
/// [`fu_rs_cab_archive_get_file`].
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_file_entry_get_name_as_unix_path(
    file: *const FileEntry,
    error: *mut *mut GError,
) -> *mut GString {
    let file = unwrap_or_panic!(file.as_ref());

    let path = file.name().as_pathbuf();
    if let Some(s) = path.to_str() {
        GString::from_rust_string(s)
    } else {
        let fe = fwupd::Error::new(
            fwupd::ErrorKind::InvalidData,
            &format!("invalid filename: {}", file.name()),
        );
        GError::convert(error, &fe);
        ptr::null_mut()
    }
}

/// Return the uncompressed size of the file in bytes.
///
/// # Safety
/// `file` must be a valid, non-`NULL` pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_file_entry_get_size(file: *const FileEntry) -> usize {
    if file.is_null() {
        return 0;
    }
    let file = unsafe { &*file };
    file.size()
}

/// Return the file attributes as a bitmask of `FuRsCabFileAttrs`.
///
/// # Safety
/// `file` must be a valid, non-`NULL` pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_file_entry_get_attributes(file: *const FileEntry) -> u32 {
    if file.is_null() {
        return 0;
    }
    let file = unsafe { &*file };
    u32::from(file.attributes().bits())
}

/// Date and time components returned by [`fu_rs_cab_file_entry_get_datetime`].
///
/// When the file has no date or time set, the respective date or time fields
/// are zero.
///
/// `second` has 2-second resolution (always even, 0–58).
#[derive(Copy, Clone, Default)]
#[repr(C)]
pub struct FuRsCabDateTime {
    pub year: u16,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
}

/// Return the file creation date and time broken into components.
///
/// When the file has no date or time set, the respective date or time fields
/// are zero.
///
/// Seconds have 2-second resolution (always even, 0–58).
///
/// # Safety
/// `file` must be a valid, non-`NULL` pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_file_entry_get_datetime(
    file: *const FileEntry,
    datetime: *mut FuRsCabDateTime,
) {
    if datetime.is_null() {
        return;
    }

    if file.is_null() {
        unsafe { *datetime = FuRsCabDateTime::default() };
    } else {
        let mut dt = FuRsCabDateTime::default();
        let file = unsafe { &*file };
        if let Some(d) = file.date() {
            dt.year = d.year();
            dt.month = d.month();
            dt.day = d.day();
        }
        let t = file.time();
        dt.hour = t.hour();
        dt.minute = t.minute();
        dt.second = t.second();
        unsafe { *datetime = dt };
    }
}

/// Read up to `count` bytes of file content into `buf`.
///
/// Returns the number of bytes read (0 at EOF) or a negative errno on error.
///
/// The file position advances with each call; the stream is reset to the
/// beginning only when the archive is first opened.
///
/// # Safety
/// `file` must be a valid, non-`NULL` pointer previously returned by
/// [`fu_rs_cab_archive_get_file`].  `buf` must point to at least `count`
/// writable bytes and must not be `NULL` when `count > 0`.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_file_entry_read(
    file: *mut FileEntry,
    buf: *mut u8,
    count: usize,
) -> isize {
    const EINVAL: isize = 22;
    if file.is_null() || (buf.is_null() && count != 0) {
        return if count == 0 { 0 } else { -EINVAL };
    }
    let file = unsafe { &mut *file };
    let slice = unsafe { std::slice::from_raw_parts_mut(buf, count) };
    match file.read(slice) {
        Ok(n) => isize::try_from(n).unwrap_or(-EINVAL),
        Err(e) => {
            if let Some(errno) = e.raw_os_error() {
                -(errno as isize)
            } else {
                -EINVAL
            }
        }
    }
}

/// Opaque handle for building an uncompressed CAB archive incrementally.
///
/// Allocated by [`fu_rs_cab_builder_new`] and freed by
/// [`fu_rs_cab_builder_free`].
pub struct CabBuilder {
    builder: Option<ArchiveBuilder<HasFiles>>,
}

/// Create a new, empty CAB builder.
///
/// Free with [`fu_rs_cab_builder_free`] when done.
#[no_mangle]
pub extern "C" fn fu_rs_cab_builder_new() -> *mut CabBuilder {
    Box::into_raw(Box::new(CabBuilder { builder: None }))
}

/// Free a CAB builder.
///
/// # Safety
/// `builder` must be a valid pointer previously returned by
/// [`fu_rs_cab_builder_new`], or `NULL` (no-op).
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_builder_free(builder: *mut CabBuilder) {
    if !builder.is_null() {
        drop(unsafe { Box::from_raw(builder) });
    }
}

/// Append a file to the CAB builder.
///
/// `name` is the NUL-terminated filename (may use `/` or `\` as separators).
/// `datetime`  is the optional timestamp.  `attrs` is the MS-DOS file
/// attribute bitmask (the UTF-8 name flag is set automatically
/// when `name` contains non-ASCII characters).
///
/// If `compress` is set, the file will be compressed.
///
/// Returns `TRUE` (1) on success, `FALSE` (0) and sets `*error` on failure.
///
/// # Safety
/// - `builder` must be a valid, non-`NULL` pointer.
/// - `name` must be a valid NUL-terminated C string.
/// - `data` must point to at least `data_len` readable bytes, or be `NULL`
///   when `data_len` is 0.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_builder_add_file(
    builder: *mut CabBuilder,
    name: *const c_char,
    data: *const u8,
    data_len: usize,
    datetime: *const FuRsCabDateTime,
    attrs: u16,
    compress: GBoolean,
    error: *mut *mut GError,
) -> i32 {
    let builder = unwrap_or_panic!(builder.as_mut());

    if name.is_null() {
        let e = fwupd::Error::new(fwupd::ErrorKind::InvalidData, "NULL argument");
        GError::convert(error, &e);
        return GFALSE;
    }
    let Ok(attrs) = MsDosFileAttrs::try_from(attrs) else {
        let e = fwupd::Error::new(fwupd::ErrorKind::InvalidData, "Invalid attributes");
        GError::convert(error, &e);
        return GFALSE;
    };
    if data_len != 0 && data.is_null() {
        let e = fwupd::Error::new(
            fwupd::ErrorKind::InvalidData,
            "Nonzero data length but no data given",
        );
        GError::convert(error, &e);
        return GFALSE;
    }

    let name_cstr = unsafe { CStr::from_ptr(name) };
    let str_name = name_cstr.to_str().unwrap_or("");
    let path = match str_name.parse::<CabPath>() {
        Ok(p) => p,
        Err(e) => {
            let fe = fwupd::Error::new(
                fwupd::ErrorKind::InvalidData,
                &format!("invalid filename: {e}"),
            );
            GError::convert(error, &fe);
            return GFALSE;
        }
    };
    let bytes = if data_len == 0 || data.is_null() {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(data, data_len) }.to_vec()
    };

    let (y, m, d, hour, min, sec) = if datetime.is_null() {
        (0, 0, 0, 0, 0, 0)
    } else {
        let dt = unsafe { *datetime };
        (dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second)
    };

    let nbytes = bytes.len();
    let stream = MemoryInputStream::from_data(bytes).into_stream();
    let file = FileBuilder::new(path, stream, nbytes as u64)
        .date(MsDosDate::from_ymd(y, m, d))
        .time(MsDosTime::from_hms(hour, min, sec))
        .attributes(attrs);
    let file = if compress != 0 { file.compress() } else { file };

    if let Some(archive_builder) = &mut builder.builder {
        archive_builder.append_file(file.build());
    } else {
        builder.builder = Some(ArchiveBuilder::new().add_file(file.build()));
    }

    GTRUE
}

/// Serialize the CAB archive into a set of bytes.
///
/// On success, returns a `GBytes` representing the
/// archive. The caller takes ownership of the `GBytes`.
///
/// The builder is consumed on calling this function
/// and the caller must free the builder without.
///
/// Returns `NULL` and sets `*error` on failure.
///
/// # Safety
/// - `builder` and `out_len` must be valid, non-`NULL` pointers.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cab_builder_build(
    builder: *mut CabBuilder,
    error: *mut *mut GError,
) -> *mut GBytes {
    let builder = unwrap_or_panic!(unsafe { builder.as_mut() });

    let Some(builder) = builder.builder.take() else {
        let e = fwupd::Error::new(
            fwupd::ErrorKind::InvalidData,
            "builder has no files (already built?)",
        );
        GError::convert(error, &e);
        return ptr::null_mut();
    };

    match builder.build(Vec::new()) {
        Ok(bytes) => GBytes::new(bytes),
        Err(e) => {
            let kind = match &e {
                BuilderError::IO(_) => fwupd::ErrorKind::Write,
                _ => fwupd::ErrorKind::InvalidData,
            };
            let fe = fwupd::Error::new(kind, &format!("failed to build CAB: {e}"));
            GError::convert(error, &fe);
            ptr::null_mut()
        }
    }
}
