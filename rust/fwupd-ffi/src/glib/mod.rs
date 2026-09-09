/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! `GLib` FFI helpers

use std::ffi::CString;
use std::os::raw::{c_char, c_int};

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

/// `GLib` log levels, matching `GLogLevelFlags` from glib.h
pub enum LogLevel {
    Error,    // 1 << 2
    Critical, // 1 << 3
    Warning,  // 1 << 4
    Message,  // 1 << 5
    Info,     // 1 << 6
    Debug,    // 1 << 7
}

impl From<LogLevel> for c_int {
    fn from(l: LogLevel) -> c_int {
        match l {
            LogLevel::Error => 1 << 2,
            LogLevel::Critical => 1 << 3,
            LogLevel::Warning => 1 << 4,
            LogLevel::Message => 1 << 5,
            LogLevel::Info => 1 << 6,
            LogLevel::Debug => 1 << 7,
        }
    }
}

#[allow(dead_code)]
extern "C" {
    fn g_log(domain: *const c_char, level: c_int, format: *const c_char, ...);
}

/// Wrapper for `GLib`'s `g_log` function.
#[allow(dead_code)]
pub(crate) fn log(level: LogLevel, message: &str) {
    unsafe {
        let msg = CString::new(message).expect("log message should not contain null bytes");
        g_log(
            std::ptr::null(),
            level.into(),
            b"%s\0".as_ptr().cast::<c_char>(),
            msg.as_ptr(),
        );
    }
}

/// Helper function to extract the function name from the type name.
/// This is a workaround for Rust not having `__func__`.
///
/// The callers define a fake function like this:
///
/// ```ignore
/// fn __type_name_helper() {}
/// function_name(__type_name_helper)
/// ```
/// This gives us the type name for the argument (`T`), in the format
/// `module::path::function_name::__type_name_helper`.
///
/// For closures we get `module::path::function_name::{{closure}}::__type_name_helper`.
///
/// We extract `function_name` and return it.
#[allow(clippy::inline_always)]
#[doc(hidden)]
#[inline(always)]
#[must_use]
pub fn function_name<T>(_: T) -> &'static str {
    let name = std::any::type_name::<T>();

    // Split by "::" and filter out __type_name_helper and {{closure}}
    let parts: Vec<&str> = name.rsplit("::").collect();

    // Skip index 0 which is __type_name_helper
    // If index 1 is {{closure}}, skip it too and use index 2
    // Otherwise use index 1
    if parts.len() > 1 {
        if parts.get(1) == Some(&"{{closure}}") && parts.len() > 2 {
            parts[2]
        } else {
            parts[1]
        }
    } else {
        name
    }
}
/// Panics if a condition is true, logging a critical error.
///
/// This macro checks a condition and panics with standardized error logging through `GLib`'s `g_log()`
/// function if the condition is true. It logs the function name and line number.
///
/// ```ignore
///     panic_if!(ptr.is_null());
///     panic_if!(ptr.is_null(), "pointer was null");
/// ```
macro_rules! panic_if {
    // condition only
    ($condition:expr) => {
        if $condition {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] condition '{}' is true",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    stringify!($condition)));
            panic!("This error is non-recoverable, panicking now");
        }
    };
    // condition, format string + args
    ($condition:expr, $fmt:expr $(, $arg:expr)*) => {
        if $condition {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] {}",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    format!($fmt $(, $arg)*)));
            panic!("This error is non-recoverable, panicking now");
        }
    };
}

/// Unwraps an `Option` or logs a critical error and panics.
///
/// This macro unwraps `Option` types with standardized error logging through `GLib`'s `g_log()`
/// function before panicking. It is similar to `expect()` but (by default) logs the
/// function name and line number.
///
/// ```ignore
///     let value = unwrap_or_panic!(option);
///     let value = unwrap_or_panic!(option, "custom error: {}", details);
/// ```
macro_rules! unwrap_or_panic {
    // option only
    ($option:expr) => {
        match $option {
            Some(val) => val,
            None => {
                fn __type_name_helper() {}
                $crate::glib::log($crate::glib::LogLevel::Critical,
                    &format!("[{}:{}] unwrapping '{}' failed: None",
                        $crate::glib::function_name(__type_name_helper), line!(),
                        stringify!($option)));
                panic!("This error is non-recoverable, panicking now");
            }
        }
    };
    // option, format string + args
    ($option:expr, $fmt:expr $(, $arg:expr)*) => {
        match $option {
            Some(val) => val,
            None => {
                fn __type_name_helper() {}
                $crate::glib::log($crate::glib::LogLevel::Critical,
                    &format!("[{}:{}] {}",
                        $crate::glib::function_name(__type_name_helper), line!(),
                        format!($fmt $(, $arg)*)));
                panic!("This error is non-recoverable, panicking now");
            }
        }
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_name_func() -> &'static str {
        fn __type_name_helper() {}
        function_name(__type_name_helper)
    }

    #[test]
    fn test_function_name_extraction() {
        fn test_func() -> &'static str {
            fn __type_name_helper() {}
            function_name(__type_name_helper)
        }
        let name = test_func();
        assert_eq!(name, "test_func");

        let name = test_name_func();
        assert_eq!(name, "test_name_func");
    }
}
