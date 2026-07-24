/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Minimal GError FFI helpers for setting errors from Rust.
//!
//! We do NOT link against GLib. Instead we declare the minimum ABI needed to
//! allocate and fill a `GError` struct through the C-side `g_set_error_literal`
//! function that the linker resolves from the GLib shared library already
//! loaded by the host process.

use std::ffi::CString;
use std::os::raw::{c_char, c_int};

use fwupd::json::JsonError;

/// Opaque GError -- we never look inside, only pass pointers.
#[repr(C)]
pub struct GError {
    pub domain: u32,
    pub code: i32,
    pub message: *const core::ffi::c_char,
}

/// Opaque GQuark (a guint32 in GLib).
type GQuark = u32;

#[allow(dead_code)]
unsafe extern "C" {
    /// `GQuark fwupd_error_quark(void)` -- defined in libfwupd.
    fn fwupd_error_quark() -> GQuark;

    /// `void g_set_error_literal(GError **err, GQuark domain, gint code, const gchar *message)`
    fn g_set_error_literal(
        err: *mut *mut GError,
        domain: GQuark,
        code: c_int,
        message: *const c_char,
    );
}

/// Set a `GError` from a [`JsonError`], if `error` is non-null.
pub(crate) unsafe fn set_gerror(error: *mut *mut GError, e: &JsonError) {
    if error.is_null() {
        return;
    }
    let (code, msg) = match e {
        JsonError::InvalidData(m) => (crate::FwupdErrorCode::InvalidData, m.as_str()),
        JsonError::WrongType(m) => (crate::FwupdErrorCode::InvalidData, m.as_str()),
        JsonError::IoError(m) => (crate::FwupdErrorCode::InvalidData, m.as_str()),
        JsonError::NotFound(m) => (crate::FwupdErrorCode::NotFound, m.as_str()),
        JsonError::NothingToDo(m) => (crate::FwupdErrorCode::NothingToDo, m.as_str()),
    };
    let c_msg =
        CString::new(msg).unwrap_or_else(|_| CString::new("(invalid error message)").unwrap());
    unsafe {
        g_set_error_literal(error, fwupd_error_quark(), code as c_int, c_msg.as_ptr());
    }
}
