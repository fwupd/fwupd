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

/// Typedef for a `gboolean`.
///
/// Boolean return values use `i32` (not Rust `bool`) to match `GLib`'s `gboolean`
/// type which is `gint` (4 bytes), not C99 `_Bool` (1 byte).
pub type GBoolean = i32;

/// Typedef for a `goffset`
pub type GOffset = i64;

/// `GLib` gboolean TRUE
pub const GTRUE: GBoolean = 1;
/// `GLib` gboolean FALSE
pub const GFALSE: GBoolean = 0;

/// Typedef for `GLib`'s `GSeekType` i32 value.
pub type GSeekType = i32;

pub const G_SEEK_CUR: GSeekType = 0;
pub const G_SEEK_SET: GSeekType = 1;
pub const G_SEEK_END: GSeekType = 2;
