/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! FFI wrappers for [`fwupd::json::JsonParser`].

use std::os::raw::{c_char, c_uint};
use std::slice;

use std::num::NonZeroU32;

use fwupd::json::{JsonParser, LoadFlags};

use super::node::FwupdRsJsonNode;
use crate::glib::set_gerror;
use crate::glib::GError;

/// Opaque FFI handle for the JSON parser.
///
/// Stores the parser limits as mutable fields (matching the C implementation)
/// and builds a fresh [`JsonParser`] on each parse call so that limit changes
/// between parses take effect immediately.
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
        max_depth: u16::MAX as u32,
        max_items: u16::MAX as u32,
        max_quoted: u16::MAX as u32,
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

/// Sets the maximum nesting depth.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_set_max_depth(
    ptr: *mut FwupdRsJsonParser,
    max_depth: c_uint,
) {
    if let Some(p) = unsafe { ptr.as_mut() } {
        p.max_depth = max_depth;
    }
}

/// Sets the maximum number of items in an array or object.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_set_max_items(
    ptr: *mut FwupdRsJsonParser,
    max_items: c_uint,
) {
    if let Some(p) = unsafe { ptr.as_mut() } {
        p.max_items = max_items;
    }
}

/// Sets the maximum length of a quoted string.
#[no_mangle]
pub unsafe extern "C" fn fwupd_rs_json_parser_set_max_quoted(
    ptr: *mut FwupdRsJsonParser,
    max_quoted: c_uint,
) {
    if let Some(p) = unsafe { ptr.as_mut() } {
        p.max_quoted = max_quoted;
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
    let p = match unsafe { ptr.as_ref() } {
        Some(p) => p,
        None => return std::ptr::null_mut(),
    };
    if text.is_null() {
        return std::ptr::null_mut();
    }
    let c_str = unsafe { std::ffi::CStr::from_ptr(text) };
    let s = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => {
            // Invalid UTF-8, but we can still try to parse the bytes
            let bytes = c_str.to_bytes();
            return load_from_bytes(p, bytes, flags, error);
        }
    };

    match p.parser().load_from_str(s, to_load_flags(flags)) {
        Ok(node) => Box::into_raw(Box::new(FwupdRsJsonNode::new(node))),
        Err(e) => {
            unsafe { set_gerror(error, &e) };
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
    let p = match unsafe { ptr.as_ref() } {
        Some(p) => p,
        None => return std::ptr::null_mut(),
    };
    if data.is_null() || data_len == 0 {
        return std::ptr::null_mut();
    }
    let bytes = unsafe { slice::from_raw_parts(data, data_len) };
    load_from_bytes(p, bytes, flags, error)
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
            unsafe { set_gerror(error, &e) };
            std::ptr::null_mut()
        }
    }
}
fn to_load_flags(flags: c_uint) -> LoadFlags {
    let mut lf = LoadFlags::NONE;
    if flags & 1 != 0 {
        lf |= LoadFlags::TRUSTED;
    }
    lf
}
