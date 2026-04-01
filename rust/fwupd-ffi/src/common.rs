/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Shared FFI helpers used by all module FFI wrappers.

use core::slice;

/// Opaque GBytes type from GLib, only used behind a pointer.
#[repr(C)]
pub(crate) struct GBytes {
    _opaque: [u8; 0],
}

/// Opaque GError type from GLib, only used behind a pointer.
#[repr(C)]
pub(crate) struct GError {
    _opaque: [u8; 0],
}

// External functions resolved at link time.
unsafe extern "C" {
    // zlib
    pub(crate) fn crc32_z(
        crc: core::ffi::c_ulong,
        buf: *const u8,
        len: usize,
    ) -> core::ffi::c_ulong;

    // GLib
    pub(crate) fn g_bytes_get_data(bytes: *const GBytes, size: *mut usize) -> *const u8;
    pub(crate) fn fwupd_error_quark() -> u32;
    pub(crate) fn g_set_error_literal(
        err: *mut *mut GError,
        domain: u32,
        code: i32,
        message: *const core::ffi::c_char,
    );
}

// FWUPD error codes matching the FwupdError enum.
#[allow(dead_code)]
pub(crate) const FWUPD_ERROR_READ: i32 = 5;
pub(crate) const FWUPD_ERROR_NOT_FOUND: i32 = 8;

/// Convert a raw pointer + length to a byte slice.
///
/// # Safety
///
/// `buf` must point to `bufsz` valid bytes, or be null with `bufsz` == 0.
pub(crate) unsafe fn buf_to_slice(buf: *const u8, bufsz: usize) -> &'static [u8] {
    if buf.is_null() || bufsz == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(buf, bufsz) }
    }
}

/// Extract a byte slice from a `GBytes*`.
///
/// # Safety
///
/// `bytes` must be a valid, non-null `GBytes*`.
pub(crate) unsafe fn gbytes_to_slice<'a>(bytes: *const GBytes) -> &'a [u8] {
    let mut size: usize = 0;
    let ptr = unsafe { g_bytes_get_data(bytes, &mut size) };
    if ptr.is_null() || size == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(ptr, size) }
    }
}
