/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Safe Rust bindings for libfwupdplugin.
//!
//! This crate provides safe, idiomatic Rust wrappers around the libfwupdplugin
//! C library, enabling fwupd plugin development in Rust.
//!
//! # Writing a Plugin
//!
//! A modular fwupd plugin is a shared library (`.so`) that exports the
//! `fu_plugin_init_vfuncs` symbol. Use the [`export_plugin!`] macro to
//! generate this entry point from a Rust type implementing [`PluginImpl`].
//!
//! ```ignore
//! use fwupdplugin::prelude::*;
//!
//! #[derive(Default)]
//! struct MyPlugin;
//!
//! impl PluginImpl for MyPlugin {
//!     fn startup(&self, plugin: &Plugin, progress: &Progress) -> Result<(), glib::Error> {
//!         Ok(())
//!     }
//! }
//!
//! fwupdplugin::export_plugin!(MyPlugin);
//! ```

/// Re-export the sys crate for advanced usage.
pub use fwupdplugin_sys as ffi;

mod context;
mod device;
mod firmware;
mod log;
// Internal implementation bits
#[doc(hidden)]
pub mod plugin;
mod progress;

pub use context::Context;
pub use device::Device;
pub use firmware::Firmware;
pub use plugin::{Plugin, PluginImpl};
pub use progress::Progress;

/// Commonly used traits and types, intended for glob import.
pub mod prelude {
    pub use super::context::Context;
    pub use super::device::{Device, DeviceExt};
    pub use super::firmware::Firmware;
    pub use super::plugin::{Plugin, PluginExt, PluginImpl};
    pub use super::progress::Progress;
    pub use glib;
}
