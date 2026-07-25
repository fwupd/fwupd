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

use std::os::raw::{c_char, c_int};

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
