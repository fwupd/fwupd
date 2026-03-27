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

/// Re-export the fwupdplugin sys crate for advanced usage.
pub use fwupdplugin_sys as ffi;

/// Re-export the fwupd sys crate for flag/enum types.
pub use fwupd_sys;

mod context;
mod device;
pub mod device_subclass;
mod firmware;
mod log;
pub mod firmware_subclass;
pub mod hid_device;
// Internal implementation bits
#[doc(hidden)]
pub mod plugin;
mod progress;
pub mod udev_device;
pub mod usb_device;

pub use context::Context;
pub use device::Device;
pub use device_subclass::DeviceImpl;
pub use firmware::Firmware;
pub use firmware_subclass::FirmwareImpl;
pub use hid_device::HidDevice;
pub use plugin::{Plugin, PluginImpl};
pub use progress::Progress;
pub use udev_device::UdevDevice;
pub use usb_device::UsbDevice;

/// Commonly used traits and types, intended for glob import.
pub mod prelude {
    pub use super::context::Context;
    pub use super::device::{Device, DeviceExt};
    pub use super::device_subclass::{DeviceImpl, DeviceImplExt};
    pub use super::firmware::Firmware;
    pub use super::firmware_subclass::{FirmwareImpl, FirmwareImplExt};
    pub use super::hid_device::{HidDevice, HidDeviceImpl};
    pub use super::plugin::{Plugin, PluginExt, PluginImpl};
    pub use super::progress::Progress;
    pub use super::udev_device::{UdevDevice, UdevDeviceImpl};
    pub use super::usb_device::{UsbDevice, UsbDeviceExt, UsbDeviceImpl};
    pub use glib;
}
