/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Minimal `GError` FFI helpers for setting errors from Rust.

use std::ffi::CString;
use std::os::raw::{c_char, c_int};

/// `GError` -- must match the `GLib` `GError` struct layout
#[repr(C)]
pub struct GError {
    pub domain: u32,
    pub code: i32,
    pub message: *mut c_char,
}

impl GError {
    /// Generic setter for a `GError` from an error code and a message.
    #[allow(dead_code)]
    pub(crate) unsafe fn set(error: *mut *mut GError, error_code: u32, msg: &str) {
        if error.is_null() {
            return;
        }

        let c_msg =
            CString::new(msg).unwrap_or_else(|_| CString::new("(invalid error message)").unwrap());
        unsafe {
            // clippy wants error_code.cast_signed() but that requires Rust 1.87.0
            #[allow(clippy::cast_possible_wrap)]
            let glib_error_code = error_code as c_int;
            g_set_error_literal(error, fwupd_error_quark(), glib_error_code, c_msg.as_ptr());
        }
    }

    /// Set the given [`GError`] from the [`fwupd::Error`]
    #[allow(dead_code)]
    pub(crate) fn convert(error: *mut *mut GError, e: &fwupd::Error) {
        unsafe {
            GError::set(error, e.kind() as u32, e.message());
        }
    }

    // Return this error's message as string
    pub(crate) fn message(&self) -> Option<String> {
        if self.message.is_null() {
            None
        } else {
            Some(unsafe {
                std::ffi::CStr::from_ptr(self.message)
                    .to_string_lossy()
                    .into_owned()
            })
        }
    }
}

/// A `GError` that we created and own in this crate.
pub(crate) struct OwnedGError(*mut GError);

impl Default for OwnedGError {
    fn default() -> Self {
        Self(std::ptr::null_mut())
    }
}

impl Drop for OwnedGError {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { g_error_free(self.0) };
        }
    }
}

impl OwnedGError {
    /// Return this error as pointer to be passed into
    /// a C function.
    pub(crate) fn as_ptr(&mut self) -> *mut *mut GError {
        std::ptr::addr_of_mut!(self.0)
    }

    /// Return this error's message as string
    pub(crate) fn message(&self) -> Option<String> {
        let e = unsafe { self.0.as_ref() };
        e.and_then(GError::message)
    }
}

/// Opaque `GQuark` (a guint32 in `GLib`).
type GQuark = u32;

#[allow(dead_code)]
extern "C" {
    /// `GQuark fwupd_error_quark(void)` -- defined in libfwupd.
    #[cfg(not(test))]
    fn fwupd_error_quark() -> GQuark;

    /// `void g_set_error_literal(GError **err, GQuark domain, gint code, const gchar *message)`
    fn g_set_error_literal(
        err: *mut *mut GError,
        domain: GQuark,
        code: c_int,
        message: *const c_char,
    );

    pub fn g_error_free(err: *mut GError);
}

/// Fake `fwupd_error_quark` so we don't need to
/// link against libfwupd just for this test
#[cfg(test)]
fn fwupd_error_quark() -> GQuark {
    0x1234
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CStr;

    #[test]
    fn convert_sets_error_code_and_message() {
        let message = "device /foo/bar not found";
        let error = fwupd::Error::new(fwupd::ErrorKind::NotFound, message);
        let mut gerr: *mut GError = std::ptr::null_mut();
        GError::convert(std::ptr::addr_of_mut!(gerr), &error);

        assert!(!gerr.is_null());
        unsafe {
            let quark = fwupd_error_quark();
            assert_eq!((*gerr).domain, quark);
            assert_eq!((*gerr).code, fwupd::ErrorKind::NotFound as i32);
            let msg = CStr::from_ptr((*gerr).message).to_str().unwrap();
            assert_eq!(msg, message);
            g_error_free(gerr);
        }
    }

    #[test]
    fn convert_internal_error() {
        let error = fwupd::Error::new(fwupd::ErrorKind::Internal, "something broke");
        let mut gerr: *mut GError = std::ptr::null_mut();
        GError::convert(std::ptr::addr_of_mut!(gerr), &error);

        assert!(!gerr.is_null());
        unsafe {
            assert_eq!((*gerr).code, 0); // Internal = 0
            let msg = CStr::from_ptr((*gerr).message).to_str().unwrap();
            assert_eq!(msg, "something broke");
            g_error_free(gerr);
        }
    }

    #[test]
    fn convert_preserves_all_error_codes() {
        let cases: &[(fwupd::ErrorKind, i32)] = &[
            (fwupd::ErrorKind::Internal, 0),
            (fwupd::ErrorKind::VersionNewer, 1),
            (fwupd::ErrorKind::VersionSame, 2),
            (fwupd::ErrorKind::AlreadyPending, 3),
            (fwupd::ErrorKind::AuthFailed, 4),
            (fwupd::ErrorKind::Read, 5),
            (fwupd::ErrorKind::Write, 6),
            (fwupd::ErrorKind::InvalidFile, 7),
            (fwupd::ErrorKind::NotFound, 8),
            (fwupd::ErrorKind::NothingToDo, 9),
            (fwupd::ErrorKind::NotSupported, 10),
            (fwupd::ErrorKind::SignatureInvalid, 11),
            (fwupd::ErrorKind::AcPowerRequired, 12),
            (fwupd::ErrorKind::PermissionDenied, 13),
            (fwupd::ErrorKind::BrokenSystem, 14),
            (fwupd::ErrorKind::BatteryLevelTooLow, 15),
            (fwupd::ErrorKind::NeedsUserAction, 16),
            (fwupd::ErrorKind::AuthExpired, 17),
            (fwupd::ErrorKind::InvalidData, 18),
            (fwupd::ErrorKind::TimedOut, 19),
            (fwupd::ErrorKind::Busy, 20),
            (fwupd::ErrorKind::NotReachable, 21),
        ];

        for &(kind, expected_code) in cases {
            let error = fwupd::Error::new(kind, "test");
            let mut gerr: *mut GError = std::ptr::null_mut();
            GError::convert(std::ptr::addr_of_mut!(gerr), &error);

            assert!(!gerr.is_null(), "GError was null for {kind:?}");
            unsafe {
                assert_eq!(
                    (*gerr).code,
                    expected_code,
                    "wrong code for {:?}: got {}, expected {}",
                    kind,
                    (*gerr).code,
                    expected_code
                );
                g_error_free(gerr);
            }
        }
    }

    #[test]
    fn convert_null_error_pointer_does_not_crash() {
        let error = fwupd::Error::new(fwupd::ErrorKind::Internal, "ignored");
        // Passing a null pointer should be a no-op, not a crash.
        GError::convert(std::ptr::null_mut(), &error);
    }

    #[test]
    fn set_null_error_pointer_does_not_crash() {
        unsafe {
            GError::set(std::ptr::null_mut(), 0, "ignored");
        }
    }
}
