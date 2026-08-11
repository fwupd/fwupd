/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! `GLib` FFI helpers

mod gerror;
mod gstring;

pub use gerror::*;
pub use gstring::*;

/// `GLib` gboolean TRUE
pub const GTRUE: i32 = 1;
/// `GLib` gboolean FALSE
pub const GFALSE: i32 = 0;

pub const G_SEEK_CUR: i32 = 0;
pub const G_SEEK_SET: i32 = 1;
pub const G_SEEK_END: i32 = 2;
