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

/// Checks a condition and returns on failure, with optional return values and
/// error messages. This provides similar functionality to `GLib`'s `g_return_if_fail()`
/// family of macros.
///
/// The macro calls through to `GLib`'s `g_log()` under the hood.
///
/// Unlike the C defines, there is only one macro that expands to the correct
/// code depending on the arguments:
///
/// ```ignore
///     return_if_fail!(<condition>);
///     return_if_fail!(<condition>, "some message fmt string", <optional format args>);
///     return_if_fail!(<condition> => <return_value>);
///     return_if_fail!(<condition> => <return_value>, "some fmt string", <optional format args>);
/// ```
///
/// For more ideomatic Rust this macro also supports pattern assignments for
/// `let ... else { return; }` expansion:
/// ```ignore
/// return_if_fail!(Some(name) = <something>);
/// return_if_fail!(Some(name) = <something> => <return_value>);
/// ```
/// that expands into the following rust code:
/// ```ignore
/// let Some(name) = <something> else { return <return_value_if_given> };
/// ```
/// See the examples below for details.
///
/// # Examples
///
/// Each macro has a version that accepts a format string and arguments for
/// a custom error message.
///
/// ## No return values
/// ```ignore
/// fn no_return_value(a: u32) {
///     return_if_fail!(a < 10);
///     return_if_fail!(a > 1, "a must be greater than 1, is {}", a);
/// }
/// ```
/// ## With return values
///
/// The return value is specified with a `=>` fat arrow:
/// ```ignore
/// fn return_value(a: u32) -> bool {
///     return_if_fail!(a < 10 => false);
///     return_if_fail!(a > 1 => false, "a must be greater than 1, is {}", a);
///     // ...
///     return true;
/// }
/// ```
/// ## Expanding to `let ... else { return; }` assignments
///
/// As above, the return value is optionally given with a `=>` fat arrow:
/// ```ignore
/// use std::os::raw::c_void;
///
/// fn let_else(ptr: *const c_void) {
///     return_if_fail!(Some(pointer) = (unsafe { ptr.as_ref() }));
///     return_if_fail!(Some(pointer) = (unsafe { ptr.as_ref() }), "ptr must not be null");
///
///     // If we get here, pointer is not null
///     assert!(!pointer.is_null());
/// }
///
/// fn let_else_return_value(ptr: *const c_void) -> i32 {
///     return_if_fail!(Some(pointer) = (unsafe { ptr.as_ref() }) => 0);
///     return_if_fail!(Some(pointer) = (unsafe { ptr.as_ref() }) => 0, "ptr must not be null");
///
///     // we know pointer is not null now
///     let _ = pointer;
///
///     return 0;
/// }
/// ```
#[macro_export]
macro_rules! return_if_fail {
    // pattern = expression
    ($pat:pat = $expr:expr) => {
        let $pat = $expr else {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] '{}' = '{}' failed",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    stringify!($pat), stringify!($expr)));
            return;
        };
    };
    // pattern = expression, format string + args
    ($pat:pat = $expr:expr, $fmt:expr $(, $arg:expr)*) => {
        let $pat = $expr else {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] {}: {}",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    format!($fmt $(, $arg)*), stringify!($expr)));
            return;
        };
    };
    // pattern = expression => return value
    ($pat:pat = $expr:expr => $ret:expr) => {
        let $pat = $expr else {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] '{}' = '{}' failed",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    stringify!($pat), stringify!($expr)));
            return $ret;
        };

    };
    // pattern = expression => return value, format string + args
    ($pat:pat = $expr:expr => $ret:expr, $fmt:expr $(, $arg:expr)*) => {
        let $pat = $expr else {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] {}",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    format!($fmt $(, $arg)*)));
            return $ret;
        };
    };
    // condition
    ($cond:expr) => {
        if !($cond) {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] assertion '{}' failed",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    stringify!($cond)));
            return;
        }
    };
    // condition, format string + args
    ($cond:expr, $fmt:expr $(, $arg:expr)*) => {
        if !($cond) {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] {}",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    format!($fmt $(, $arg)*)));
            return;
        }
    };
    // condition => return value
    ($cond:expr => $ret:expr) => {
        if !($cond) {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] assertion '{}' failed",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    stringify!($cond)));
            return $ret;
        }

    };
    // condition => return value, format string + args
    ($cond:expr => $ret:expr, $fmt:expr $(, $arg:expr)*) => {
        if !($cond) {
            fn __type_name_helper() {}
            $crate::glib::log($crate::glib::LogLevel::Critical,
                &format!("[{}:{}] {}",
                    $crate::glib::function_name(__type_name_helper), line!(),
                    format!($fmt $(, $arg)*)));
            return $ret;
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

    #[no_mangle]
    extern "C" fn test_name_no_mangle_func() {
        fn __type_name_helper() {}
        let name = function_name(__type_name_helper);
        assert_eq!(name, "test_name_no_mangle_func");
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

    #[test]
    fn test_return_if_fail() {
        // Funny variable name to make the testlog.txt less confusing
        fn helper(x_ignore_error_in_testlog: i32) {
            return_if_fail!(x_ignore_error_in_testlog > 0);
            return_if_fail!(
                x_ignore_error_in_testlog < 10,
                "x must be under 10: {} [this is a test message]",
                x_ignore_error_in_testlog
            );
        }

        // Compile-test only
        helper(0);
        helper(5);
        helper(10);
    }

    #[test]
    fn test_return_val_if_fail() {
        // Funny variable name to make the testlog.txt less confusing
        fn helper(x_ignore_error_in_testlog: i32) -> i32 {
            return_if_fail!(x_ignore_error_in_testlog > 0 => -1);
            return_if_fail!(x_ignore_error_in_testlog < 10 => -2, "x must be under 10: {} [this is a test message]", x_ignore_error_in_testlog);
            x_ignore_error_in_testlog * 2
        }

        assert_eq!(helper(0), -1);
        assert_eq!(helper(5), 10);
        assert_eq!(helper(11), -2);
    }

    #[test]
    fn test_pattern_if_fail() {
        // Funny variable name to make the testlog.txt less confusing
        fn helper(o: Option<u8>) {
            return_if_fail!(Some(i_ignore_error_in_testlog) = o);
            let _ = i_ignore_error_in_testlog;
        }
        fn helper2(o: Option<u8>) {
            return_if_fail!(Some(i) = o, "o must not be None [this is a test message]");
            let _ = i;
        }

        // Compile-test only
        helper(None);
        helper(Some(5));
        helper2(None);
        helper2(Some(5));
    }

    #[test]
    fn test_pattern_return_val_if_fail() {
        // Funny variable name to make the testlog.txt less confusing
        fn helper(o: Option<u8>) -> i32 {
            return_if_fail!(Some(i_ignore_error_in_testlog) = o => -1);
            i32::from(i_ignore_error_in_testlog)
        }
        fn helper2(o: Option<u8>) -> i32 {
            return_if_fail!(Some(i) = o => -2, "o must not be None [this is a test message]");
            i32::from(i)
        }
        // Compile-test only
        assert_eq!(helper(None), -1);
        assert_eq!(helper(Some(5)), 5);
        assert_eq!(helper2(None), -2);
        assert_eq!(helper2(Some(5)), 5);
    }
}
