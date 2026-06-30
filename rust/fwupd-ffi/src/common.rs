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

/// GLib `GError` with the fields we need to read.
///
/// The real struct has `domain` (GQuark), `code` (gint), and `message`
/// (gchar*).  We only read `message`; the first two fields are
/// declared so the layout matches.
#[repr(C)]
pub struct GError {
    pub domain: u32,
    pub code: i32,
    pub message: *const core::ffi::c_char,
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
    pub fn g_memdup2(mem: *const u8, byte_size: usize) -> *mut u8;
    pub fn fu_strsafe(str: *const core::ffi::c_char, maxsz: usize) -> *mut core::ffi::c_char;
    pub fn g_string_new_len(init: *const core::ffi::c_char, len: isize) -> *mut GString;
    pub fn g_error_free(error: *mut GError);

    // GIO -- GInputStream / GSeekable
    pub fn g_object_ref(object: *mut core::ffi::c_void) -> *mut core::ffi::c_void;
    pub fn g_object_unref(object: *mut core::ffi::c_void);
    pub fn g_seekable_can_seek(seekable: *mut core::ffi::c_void) -> i32;
    pub fn g_seekable_seek(
        seekable: *mut core::ffi::c_void,
        offset: i64,
        type_: i32,
        cancellable: *mut core::ffi::c_void,
        error: *mut *mut GError,
    ) -> i32;
    pub fn g_seekable_tell(seekable: *mut core::ffi::c_void) -> i64;
    pub fn g_input_stream_read(
        stream: *mut core::ffi::c_void,
        buffer: *mut u8,
        count: usize,
        cancellable: *mut core::ffi::c_void,
        error: *mut *mut GError,
    ) -> isize;
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

/// Extract the message from a `GError*`, free it, and return a Rust `String`.
///
/// Returns `"unknown error"` if `gerror` is null or the message is not
/// valid UTF-8.
///
/// # Safety
///
/// `gerror` must be a valid `GError*` allocated by GLib, or null.
pub unsafe fn take_gerror_message(gerror: *mut GError) -> String {
    if gerror.is_null() {
        return "unknown error".to_string();
    }
    let msg = unsafe { &*gerror }.message;
    let s = if msg.is_null() {
        "unknown error".to_string()
    } else {
        unsafe { core::ffi::CStr::from_ptr(msg) }
            .to_str()
            .unwrap_or("unknown error")
            .to_string()
    };
    unsafe { g_error_free(gerror) };
    s
}

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
