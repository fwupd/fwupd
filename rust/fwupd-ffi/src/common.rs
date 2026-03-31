/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Shared FFI helpers used by all module FFI wrappers.

use core::slice;

/// Opaque GBytes type from GLib, only used behind a pointer.
#[repr(C)]
pub struct GBytes {
    _opaque: [u8; 0],
}

/// Opaque GError type from GLib, only used behind a pointer.
#[repr(C)]
pub(crate) struct GError {
    _opaque: [u8; 0],
}

// External functions resolved at link time.
unsafe extern "C" {
    // GLib
    pub fn g_bytes_get_data(bytes: *const GBytes, size: *mut usize) -> *const u8;
    pub fn fwupd_error_quark() -> u32;
    pub fn g_set_error_literal(
        err: *mut *mut GError,
        domain: u32,
        code: i32,
        message: *const core::ffi::c_char,
    );
    pub(crate) fn g_memdup2(mem: *const u8, byte_size: usize) -> *mut u8;
    pub(crate) fn fu_strsafe(str: *const core::ffi::c_char, maxsz: usize)
    -> *mut core::ffi::c_char;
    pub(crate) fn g_string_new_len(init: *const core::ffi::c_char, len: isize) -> *mut GString;
}

/// Opaque GString type from GLib, only used behind a pointer.
#[repr(C)]
pub struct GString {
    _opaque: [u8; 0],
}

// FWUPD error codes matching the FwupdError enum.
pub const FWUPD_ERROR_READ: i32 = 5;
pub const FWUPD_ERROR_WRITE: i32 = 6;
pub const FWUPD_ERROR_NOT_FOUND: i32 = 8;
pub const FWUPD_ERROR_NOT_SUPPORTED: i32 = 10;
pub const FWUPD_ERROR_INVALID_DATA: i32 = 18;

/// Convert a raw pointer + length to a byte slice.
///
/// # Safety
///
/// `buf` must point to `bufsz` valid bytes, or be null with `bufsz` == 0.
pub unsafe fn buf_to_slice<'a>(buf: *const u8, bufsz: usize) -> &'a [u8] {
    if buf.is_null() || bufsz == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(buf, bufsz) }
    }
}

/// Convert a raw pointer + length to a mutable byte slice.
pub unsafe fn buf_to_slice_mut<'a>(buf: *mut u8, bufsz: usize) -> &'a mut [u8] {
    if buf.is_null() || bufsz == 0 {
        &mut []
    } else {
        unsafe { slice::from_raw_parts_mut(buf, bufsz) }
    }
}

/// Extract a byte slice from a `GBytes*`.
///
/// # Safety
///
/// `bytes` must be a valid, non-null `GBytes*`.
pub unsafe fn gbytes_to_slice<'a>(bytes: *const GBytes) -> &'a [u8] {
    let mut size: usize = 0;
    let ptr = unsafe { g_bytes_get_data(bytes, &mut size) };
    if ptr.is_null() || size == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(ptr, size) }
    }
}
