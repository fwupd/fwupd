/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Minimal GString FFI helpers for returning GLib strings from Rust.

use std::ffi::CString;
use std::os::raw::c_char;

/// Opaque GString -- matches the GLib `GString` struct layout.
#[repr(C)]
pub struct GString {
    pub str_: *mut c_char,
    pub len: usize,
    pub allocated_len: usize,
}

#[allow(dead_code)]
unsafe extern "C" {
    fn g_string_new(init: *const c_char) -> *mut GString;
    fn g_string_append_len(string: *mut GString, val: *const c_char, len: isize) -> *mut GString;
}

impl GString {
    /// Create a new `GString *` from a Rust string. Returns null on failure.
    pub fn from_rust_string(s: &str) -> *mut GString {
        let cs = match CString::new(s) {
            Ok(cs) => cs,
            Err(_) => return std::ptr::null_mut(),
        };
        unsafe {
            let gstr = g_string_new(std::ptr::null());
            if gstr.is_null() {
                return std::ptr::null_mut();
            }
            g_string_append_len(gstr, cs.as_ptr(), cs.as_bytes().len() as isize);
            gstr
        }
    }
}
