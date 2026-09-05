/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! C-compatible FFI wrappers for the fwupd JSON parser.
//!
//! These functions are intended to be called from the existing libfwupd C code,
//! providing an identical interface to the original C implementation but backed
//! by the Rust `fwupd::json` module.
//!
//! # Naming convention
//!
//! All FFI functions use a `fwupd_rs_json_` prefix (e.g. `fwupd_rs_json_parser_new`)
//! so both implementations can coexist during migration.
//!
//! # Memory ownership
//!
//! - Functions returning `*mut T` transfer ownership to the caller.
//! - Functions taking `*const T` or `*mut T` borrow (the caller retains ownership).
//! - `_free` functions consume the pointer and deallocate.
//! - Strings returned as `*const c_char` point into the Rust-owned data and are
//!   valid until the owning object is freed.

use fwupd::json::{ExportFlag, ExportFlags};
use fwupd::Bitflags;
use std::os::raw::c_uint;

/// Convert a `None` into a [`NotFound`](fwupd::ErrorKind::NotFound) error.
macro_rules! notfound {
    ($($arg:tt)*) => {
        fwupd::Error::new(fwupd::ErrorKind::NotFound, &format!($($arg)*))
    };
}

mod array;
mod node;
mod object;
mod parser;

/// Convert C export flags bitmask to the Rust [`ExportFlags`] type.
pub(crate) fn to_export_flags(flags: c_uint) -> ExportFlags {
    let mut ef = ExportFlags::empty();
    if flags & 1 != 0 {
        ef |= ExportFlag::Indent;
    }
    if flags & 2 != 0 {
        ef |= ExportFlag::TrailingNewline;
    }
    ef
}
