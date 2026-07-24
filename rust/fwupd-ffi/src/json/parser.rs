/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! FFI wrappers for [`fwupd::json::JsonParser`].

use std::io::{Seek, SeekFrom};
use std::os::raw::{c_char, c_uint};
use std::slice;

use std::num::NonZeroU32;

use fwupd::json::{JsonParser, LoadFlag, LoadFlags};
use fwupd::streams::{CStream, CStreamCallbacks, CStreamHandle, IsSeekable};
use fwupd::Bitflags;

use super::node::FwupdRsJsonNode;
use crate::glib::GError;
use crate::streams::StreamImpl;

/// Opaque FFI handle for the JSON parser.
///
/// Stores the parser limits as mutable fields (matching the C implementation)
/// and builds a fresh [`JsonParser`] on each parse call so that limit changes
/// between parses take effect immediately.
#[allow(clippy::struct_field_names)]
pub struct FwupdRsJsonParser {
    max_depth: u32,
    max_items: u32,
    max_quoted: u32,
}

impl FwupdRsJsonParser {
    /// Build a [`JsonParser`] from the current limits.
    fn parser(&self) -> JsonParser {
        // SAFETY: defaults are u16::MAX (non-zero), and the C set_max_*
        // functions clamp to non-zero via NonZeroU32.
        let depth = NonZeroU32::new(self.max_depth).unwrap_or(NonZeroU32::new(1).unwrap());
        let items = NonZeroU32::new(self.max_items).unwrap_or(NonZeroU32::new(1).unwrap());
        let quoted = NonZeroU32::new(self.max_quoted).unwrap_or(NonZeroU32::new(1).unwrap());
        JsonParser::builder()
            .max_depth(depth)
            .max_items(items)
            .max_quoted(quoted)
            .build()
    }
}

/// Creates a new JSON parser.
///
/// The caller owns the returned pointer and must free it with
/// [`fwupd_rs_json_parser_free`].
#[no_mangle]
pub extern "C" fn fwupd_rs_json_parser_new() -> *mut FwupdRsJsonParser {
    let parser = FwupdRsJsonParser {
        max_depth: u32::from(u16::MAX),
        max_items: u32::from(u16::MAX),
        max_quoted: u32::from(u16::MAX),
    };
    Box::into_raw(Box::new(parser))
}

/// Frees a JSON parser.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_free(ptr: *mut FwupdRsJsonParser) {
    if !ptr.is_null() {
        drop(unsafe { Box::from_raw(ptr) });
    }
}

/// Sets the maximum nesting depth. A value of 0 is clamped to 1.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_set_max_depth(
    ptr: *mut FwupdRsJsonParser,
    max_depth: c_uint,
) {
    if let Some(p) = unsafe { ptr.as_mut() } {
        p.max_depth = max_depth.max(1);
    }
}

/// Sets the maximum number of items in an array or object. A value of 0 is
/// clamped to 1.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_set_max_items(
    ptr: *mut FwupdRsJsonParser,
    max_items: c_uint,
) {
    if let Some(p) = unsafe { ptr.as_mut() } {
        p.max_items = max_items.max(1);
    }
}

/// Sets the maximum length of a quoted string. A value of 0 is clamped to 1.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_set_max_quoted(
    ptr: *mut FwupdRsJsonParser,
    max_quoted: c_uint,
) {
    if let Some(p) = unsafe { ptr.as_mut() } {
        p.max_quoted = max_quoted.max(1);
    }
}

/// Loads JSON from a NUL-terminated C string.
///
/// Returns a new `FwupdRsJsonNode` on success, or `NULL` on error (with
/// `error` set if non-NULL).
///
/// The `flags` parameter maps to `FwupdJsonLoadFlags`:
/// - 0 = none
/// - 1 = trusted (skip duplicate key checks)
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_load_from_data(
    ptr: *const FwupdRsJsonParser,
    text: *const c_char,
    flags: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonNode {
    let Some(p) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    if text.is_null() {
        return std::ptr::null_mut();
    }
    let c_str = unsafe { std::ffi::CStr::from_ptr(text) };
    let Ok(s) = c_str.to_str() else {
        // Invalid UTF-8, but we can still try to parse the bytes
        let bytes = c_str.to_bytes();
        return load_from_bytes(p, bytes, flags, error);
    };

    match p.parser().load_from_str(s, to_load_flags(flags)) {
        Ok(node) => Box::into_raw(Box::new(FwupdRsJsonNode::new(node))),
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
    }
}

/// Loads JSON from a byte buffer with explicit length.
///
/// Returns a new `FwupdRsJsonNode` on success, or `NULL` on error.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_load_from_bytes(
    ptr: *const FwupdRsJsonParser,
    data: *const u8,
    data_len: usize,
    flags: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonNode {
    let Some(p) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    if data.is_null() || data_len == 0 {
        return std::ptr::null_mut();
    }
    let bytes = unsafe { slice::from_raw_parts(data, data_len) };
    load_from_bytes(p, bytes, flags, error)
}

/// Loads JSON by streaming from a C-side `GInputStream` via [`CStream`].
///
/// The stream is wrapped into a [`CStream`] using the provided `handle` and
/// `callbacks`, seeked to the start if seekable, and then passed directly to
/// the streaming JSON tokenizer without buffering the entire content first.
///
/// Returns a new `FwupdRsJsonNode` on success, or `NULL` on error.
///
/// # Safety
/// - `ptr` must be a valid `FwupdRsJsonParser` pointer.
/// - `handle` must be a valid C-side `GInputStream` handle.
/// - `callbacks` must contain valid function pointers.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_load_from_stream(
    ptr: *const FwupdRsJsonParser,
    handle: CStreamHandle,
    callbacks: CStreamCallbacks,
    flags: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonNode {
    let Some(p) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    if handle.is_null() {
        return std::ptr::null_mut();
    }
    let mut cstream = CStream::new(handle, callbacks);

    // Seek to start if possible (matching the C-side behavior)
    if cstream.is_seekable() {
        if let Err(e) = cstream.seek(SeekFrom::Start(0)) {
            GError::convert(error, &e.into());
            return std::ptr::null_mut();
        }
    }

    match p
        .parser()
        .load_from_reader(&mut cstream, to_load_flags(flags))
    {
        Ok(node) => Box::into_raw(Box::new(FwupdRsJsonNode::new(node))),
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
    }
}

/// Loads JSON from a Rust-backed stream via its [`StreamImpl`] handle.
///
/// This is the fast path for streams that already have a Rust implementation
/// (i.e. all [`FuInputStream`] subclasses). The stream impl is used directly
/// without any C callback indirection.
///
/// Ownership of `stream_impl` is transferred to this function; the caller
/// must not free it afterwards.
///
/// Returns a new `FwupdRsJsonNode` on success, or `NULL` on error.
///
/// # Safety
/// - `ptr` must be a valid `FwupdRsJsonParser` pointer.
/// - `stream_impl` must be a valid `StreamImpl` pointer previously returned
///   by an `_get_stream_impl` function.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_load_from_stream_impl(
    ptr: *const FwupdRsJsonParser,
    stream_impl: *mut StreamImpl,
    flags: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonNode {
    let Some(p) = (unsafe { ptr.as_ref() }) else {
        return std::ptr::null_mut();
    };
    if stream_impl.is_null() {
        return std::ptr::null_mut();
    }

    // Take ownership of the StreamImpl (Arc<Mutex<dyn ReadSeek + Send>>).
    let arc = unsafe { *Box::from_raw(stream_impl) };
    let mut guard = arc.lock().unwrap();

    // Seek to start
    if let Err(e) = guard.seek(SeekFrom::Start(0)) {
        GError::convert(error, &e.into());
        return std::ptr::null_mut();
    }

    match p
        .parser()
        .load_from_reader(&mut *guard, to_load_flags(flags))
    {
        Ok(node) => Box::into_raw(Box::new(FwupdRsJsonNode::new(node))),
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
    }
}

fn load_from_bytes(
    p: &FwupdRsJsonParser,
    bytes: &[u8],
    flags: c_uint,
    error: *mut *mut GError,
) -> *mut FwupdRsJsonNode {
    match p.parser().load_from_bytes(bytes, to_load_flags(flags)) {
        Ok(node) => Box::into_raw(Box::new(FwupdRsJsonNode::new(node))),
        Err(e) => {
            GError::convert(error, &(&e).into());
            std::ptr::null_mut()
        }
    }
}
fn to_load_flags(flags: c_uint) -> LoadFlags {
    let mut lf = LoadFlags::empty();
    if flags & 1 != 0 {
        lf |= LoadFlag::TRUSTED;
    }
    lf
}
