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
extern "C" {
    fn g_string_new(init: *const c_char) -> *mut GString;
    fn g_string_append_len(string: *mut GString, val: *const c_char, len: isize) -> *mut GString;
}

impl GString {
    /// Create a new `GString *` from a Rust string.
    ///
    /// The returned `GString` is owned by the caller and must be freed accordingly.
    pub fn from_rust_string(s: &str) -> *mut GString {
        let cs = CString::new(s).expect("Failed to allocate string");
        unsafe {
            let gstr = g_string_new(cs.as_ptr());
            if gstr.is_null() {
                panic!("g_string_new() returned NULL");
            }
            gstr
        }
    }
}
