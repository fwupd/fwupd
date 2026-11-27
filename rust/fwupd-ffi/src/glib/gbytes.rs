/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Minimal `GBytes` FFI helpers for returning `GBytes` from Rust.

use std::ffi::CString;
use std::os::raw::c_void;

/// Opaque `GBytes` representing `GLib`'s `GBytes`.
///
/// This is an opaque type in `GLib` so we merely wrap the pointer.
#[repr(C)]
pub struct GBytes {
    _private: [u8; 0],
}

pub type GDestroyNotify = Option<unsafe extern "C" fn(data: *mut c_void)>;

#[allow(dead_code)]
#[link(name = "glib-2.0")]
extern "C" {
    fn g_bytes_new_with_free_func(
        data: *const c_void,
        size: usize,
        free_func: GDestroyNotify,
        user_data: *mut c_void,
    ) -> *mut GBytes;
}

unsafe extern "C" fn free_data(user_data: *mut c_void) {
    if !user_data.is_null() {
        // Rebuilds the Box and drops it, deallocating the Vec's underlying byte buffer.
        drop(Box::from_raw(user_data.cast::<Vec<u8>>()));
    }
}

impl GBytes {
    /// Create a new `GBytes` from a Rust `Vec<u8>`.
    ///
    /// The returned `GBytes` is owned by the caller and
    /// releases the bytes on unref.
    #[must_use]
    pub fn new(data: Vec<u8>) -> *mut GBytes {
        let mut boxed = Box::new(data);
        let size = boxed.len();

        let data_ptr = if size == 0 {
            std::ptr::null()
        } else {
            boxed.as_ptr().cast::<c_void>()
        };

        let user_data = Box::into_raw(boxed).cast::<c_void>();

        unsafe { g_bytes_new_with_free_func(data_ptr, size, Some(free_data), user_data) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[link(name = "glib-2.0")]
    extern "C" {
        fn g_bytes_get_data(bytes: *const GBytes, size: *mut usize) -> *const c_void;
        fn g_bytes_get_size(bytes: *const GBytes) -> usize;
        fn g_bytes_unref(bytes: *mut GBytes);
    }

    #[test]
    fn new_returns_non_null_for_non_empty_data() {
        let gbytes = GBytes::new(vec![1u8, 2, 3, 4, 5]);
        assert!(!gbytes.is_null());
        unsafe { g_bytes_unref(gbytes) };
    }

    #[test]
    fn new_returns_non_null_for_empty_data() {
        let gbytes = GBytes::new(vec![]);
        assert!(!gbytes.is_null());
        unsafe { g_bytes_unref(gbytes) };
    }

    #[test]
    fn new_preserves_size() {
        let gbytes = GBytes::new(vec![0xDE_u8, 0xAD, 0xBE, 0xEF]);
        assert!(!gbytes.is_null());
        assert_eq!(unsafe { g_bytes_get_size(gbytes) }, 4);
        unsafe { g_bytes_unref(gbytes) };
    }

    #[test]
    fn new_preserves_content() {
        let gbytes = GBytes::new(vec![0xDE_u8, 0xAD, 0xBE, 0xEF]);
        assert!(!gbytes.is_null());
        unsafe {
            let mut size = 0usize;
            let ptr = g_bytes_get_data(gbytes, std::ptr::addr_of_mut!(size));
            assert!(!ptr.is_null());
            assert_eq!(size, 4);
            let slice = std::slice::from_raw_parts(ptr.cast::<u8>(), size);
            assert_eq!(slice, &[0xDE, 0xAD, 0xBE, 0xEF]);
        }
        unsafe { g_bytes_unref(gbytes) };
    }

    #[test]
    fn empty_gbytes_has_zero_size() {
        let gbytes = GBytes::new(vec![]);
        assert!(!gbytes.is_null());
        assert_eq!(unsafe { g_bytes_get_size(gbytes) }, 0);
        unsafe { g_bytes_unref(gbytes) };
    }
}
