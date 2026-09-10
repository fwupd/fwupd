/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Minimal `GRefString` FFI helpers for returning `GRefString` strings from Rust.

use std::ffi::CString;
use std::os::raw::c_char;

/// Opaque `GRefString` representing `GLib`'s `GRefString`.
///
/// This is an opaque type in `GLib` so we merely wrap the pointer.
#[repr(C)]
pub struct GRefString {
    _private: [u8; 0],
}

#[allow(dead_code)]
extern "C" {
    fn g_ref_string_new(s: *const c_char) -> *mut GRefString;
}

impl GRefString {
    /// Create a new `GRefString` from a Rust string
    ///
    /// The returned `GRefString` has a refcount of 1.
    ///
    /// # Panics
    /// This function panics if `s` contains a null byte.
    #[must_use]
    pub fn from_rust_string(s: &str) -> *mut GRefString {
        let cs = CString::new(s).expect("Unexpected null byte in string");
        unsafe {
            let gstr = g_ref_string_new(cs.as_ptr());
            assert!(!gstr.is_null(), "g_ref_string_new() returned NULL");
            gstr
        }
    }
}
