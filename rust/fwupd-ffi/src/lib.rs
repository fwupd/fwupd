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

pub mod common;
pub mod crc;
pub mod xor;
