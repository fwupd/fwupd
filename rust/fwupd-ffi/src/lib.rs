/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

// All public functions in this crate are unsafe extern "C" FFI wrappers.
// The safety contract is uniform: callers must pass valid C pointers as
// documented in the corresponding C header files.
#![allow(clippy::missing_safety_doc)]

//! C-compatible FFI bindings for the fwupd Rust implementations.
//!
//! This crate provides `#[no_mangle] extern "C"` wrappers around the pure-Rust
//! `fwupd` crate, producing a static library that can be linked into
//! `libfwupdplugin.so` to replace the C implementations.

pub mod glib;
pub mod json;

/// FwupdError enum values matching the C `FwupdError` enum order from
/// `fwupd-error.rs` (the DSL file, not Rust code).
#[repr(C)]
#[allow(dead_code)]
pub(crate) enum FwupdErrorCode {
    Internal = 0,
    VersionNewer = 1,
    VersionSame = 2,
    AlreadyPending = 3,
    AuthFailed = 4,
    Read = 5,
    Write = 6,
    InvalidFile = 7,
    NotFound = 8,
    NothingToDo = 9,
    NotSupported = 10,
    SignatureInvalid = 11,
    AcPowerRequired = 12,
    PermissionDenied = 13,
    BrokenSystem = 14,
    BatteryLevelTooLow = 15,
    NeedsUserAction = 16,
    AuthExpired = 17,
    InvalidData = 18,
    TimedOut = 19,
    Busy = 20,
    NotReachable = 21,
}
