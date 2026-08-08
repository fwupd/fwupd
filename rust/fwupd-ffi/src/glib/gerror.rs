/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Minimal GError FFI helpers for setting errors from Rust.

use std::ffi::CString;
use std::os::raw::{c_char, c_int};

/// GError -- must match the GLib GError struct layout
#[repr(C)]
pub struct GError {
    pub domain: u32,
    pub code: i32,
    pub message: *const c_char,
}

impl GError {
    /// Generic setter for a GError from an error code and a message.
    #[allow(dead_code)]
    pub(crate) unsafe fn set(error: *mut *mut GError, error_code: u32, msg: &str) {
        if error.is_null() {
            return;
        }

        let c_msg =
            CString::new(msg).unwrap_or_else(|_| CString::new("(invalid error message)").unwrap());
        unsafe {
            g_set_error_literal(
                error,
                fwupd_error_quark(),
                error_code as c_int,
                c_msg.as_ptr(),
            );
        }
    }
}

/// Opaque GQuark (a guint32 in GLib).
type GQuark = u32;

#[allow(dead_code)]
extern "C" {
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
