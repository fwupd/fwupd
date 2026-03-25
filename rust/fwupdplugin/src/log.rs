/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Logging macros for fwupd plugins.
//!
//! These are thin wrappers around GLib's logging functions (`g_debug`,
//! `g_info`, `g_message`, `g_warning`, `g_critical`). The log domain
//! is automatically set to the GObject type name of the plugin or device.
//!
//! # Example
//!
//! ```ignore
//! use fwupdplugin::{fu_debug, fu_warning};
//!
//! fu_debug!("FuMyPlugin", "startup called");
//! fu_warning!("FuMyPlugin", "something unexpected: {}", value);
//! ```

/// Log a debug message. Requires `--verbose` to be visible in fwupdtool.
///
/// ```ignore
/// fu_debug!("FuMyPlugin", "value is {}", x);
/// ```
#[macro_export]
macro_rules! fu_debug {
    ($domain:expr, $($arg:tt)*) => {
        $crate::prelude::glib::g_debug!($domain, $($arg)*)
    };
}

/// Log an info message.
///
/// ```ignore
/// fu_info!("FuMyPlugin", "plugin loaded");
/// ```
#[macro_export]
macro_rules! fu_info {
    ($domain:expr, $($arg:tt)*) => {
        $crate::prelude::glib::g_info!($domain, $($arg)*)
    };
}

/// Log a message (normal priority, always shown).
///
/// ```ignore
/// fu_message!("FuMyPlugin", "device found: {}", name);
/// ```
#[macro_export]
macro_rules! fu_message {
    ($domain:expr, $($arg:tt)*) => {
        $crate::prelude::glib::g_message!($domain, $($arg)*)
    };
}

/// Log a warning message.
///
/// ```ignore
/// fu_warning!("FuMyPlugin", "quirk not found for {}", guid);
/// ```
#[macro_export]
macro_rules! fu_warning {
    ($domain:expr, $($arg:tt)*) => {
        $crate::prelude::glib::g_warning!($domain, $($arg)*)
    };
}

/// Log a critical message.
///
/// ```ignore
/// fu_critical!("FuMyPlugin", "unexpected state: {}", state);
/// ```
#[macro_export]
macro_rules! fu_critical {
    ($domain:expr, $($arg:tt)*) => {
        $crate::prelude::glib::g_critical!($domain, $($arg)*)
    };
}
