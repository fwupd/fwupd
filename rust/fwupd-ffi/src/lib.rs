/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! C-compatible FFI bindings for the fwupd Rust implementations.
//!
//! This crate provides `#[no_mangle] extern "C"` wrappers around the pure-Rust
//! `fwupd` crate, producing a static library that can be linked into
//! `libfwupdplugin.so` to replace the C implementations.

mod common;
mod crc;
