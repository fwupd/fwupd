/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Error helpers for fwupd, most notably the [define_error] macro.

/// A poor man's version of thiserror that does not have external dependencies.
///
/// ```ignore
/// fwupd::define_error! {
///     SomeError {
///         // Error with a static error message
///         VariantA => "something went wrong",
///         // Tuple-payload variant (inner type must implement Display + Debug).
///         // Display prints "prefix {inner_value}".
///         VariantB(String) => "Prefix:",
///         VariantC(u32) => "Error code",
///     }
/// }
/// assert_eq!(SomeError::VariantA.to_string(), "something went wrong");
/// assert_eq!(
///     SomeError::VariantB("bad input".into()).to_string(),
///     "Prefix: bad input",
/// );
/// assert_eq!(SomeError::VariantC(42).to_string(), "Error code 42");
///
/// // Explicit discriminants work when all variants are unit variants.
/// fwupd::define_error! {
///     CodedError {
///         First = 0 => "first error",
///         Second = 1 => "second error",
///     }
/// }
/// assert_eq!(CodedError::First.to_string(), "first error");
/// ```
///
/// All forms accept `#[...]` attributes on both the enum and each variant
/// (e.g. `/// doc comments`, `#[derive(...)]`). The macro always adds
/// `#[derive(Debug)]`; callers should not repeat it.
///
/// The resulting enum implements [Display](std::fmt::Display) and
/// [Error](std::error::Error).
#[macro_export]
macro_rules! define_error {
    // Entry point: strip enum-level attributes, open the TT muncher.
    ($(#[$enum_meta:meta])* $name:ident { $($body:tt)* }) => {
        $crate::define_error!(@munch $(#[$enum_meta])* $name [] [] $($body)*);
    };

    // ── TT muncher: one variant at a time ──────────────────────────
    //
    // Accumulates two token lists:
    //   [variants] - enum variant declarations
    //   [arms]     - match arms as { pattern => (fmt, arg...) ; } groups
    //               expanded into write!() calls in the terminal arm

    // Unit variant with explicit discriminant: Variant = val => "msg"
    (@munch $(#[$enum_meta:meta])* $name:ident
        [ $($variants:tt)* ] [ $($arms:tt)* ]
        $(#[$meta:meta])* $variant:ident = $val:expr => $msg:expr,
        $($rest:tt)*
    ) => {
        $crate::define_error!(@munch $(#[$enum_meta])* $name
            [ $($variants)* $(#[$meta])* #[doc = $msg] $variant = $val, ]
            [ $($arms)* { $name::$variant => ($msg) } ]
            $($rest)*
        );
    };

    // Unit variant without discriminant: Variant => "msg"
    (@munch $(#[$enum_meta:meta])* $name:ident
        [ $($variants:tt)* ] [ $($arms:tt)* ]
        $(#[$meta:meta])* $variant:ident => $msg:expr,
        $($rest:tt)*
    ) => {
        $crate::define_error!(@munch $(#[$enum_meta])* $name
            [ $($variants)* $(#[$meta])* #[doc = $msg] $variant, ]
            [ $($arms)* { $name::$variant => ($msg) } ]
            $($rest)*
        );
    };

    // Tuple-payload variant: Variant(Type) => "prefix"
    (@munch $(#[$enum_meta:meta])* $name:ident
        [ $($variants:tt)* ] [ $($arms:tt)* ]
        $(#[$meta:meta])* $variant:ident($ty:ty) => $prefix:expr,
        $($rest:tt)*
    ) => {
        $crate::define_error!(@munch $(#[$enum_meta])* $name
            [ $($variants)* $(#[$meta])* $variant($ty), ]
            [ $($arms)* { $name::$variant(inner) => (concat!($prefix, " {}"), inner) } ]
            $($rest)*
        );
    };

    // ── Terminal: no more variants, emit the enum ──────────────────

    (@munch $(#[$enum_meta:meta])* $name:ident
        [ $($variants:tt)* ] [ $({ $pat:pat => ($($fmt_args:expr),*) })* ]
    ) => {
        $(#[$enum_meta])*
        #[derive(Debug)]
        pub enum $name {
            $($variants)*
        }

        impl std::fmt::Display for $name {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                match self {
                    $($pat => write!(f, $($fmt_args),*),)*
                }
            }
        }

        impl std::error::Error for $name {}
    };
}

#[cfg(test)]
mod tests {
    define_error! {
        TestError {
            VariantA => "something went wrong",
            VariantB(String) => "Prefix:",
            VariantC(u32) => "Error code",
        }
    }

    define_error! {
        CodedError {
            First = 0 => "first error",
            Second = 1 => "second error",
        }
    }

    #[test]
    fn unit_variant_display() {
        assert_eq!(TestError::VariantA.to_string(), "something went wrong");
    }

    #[test]
    fn string_payload_display() {
        assert_eq!(
            TestError::VariantB("bad input".into()).to_string(),
            "Prefix: bad input",
        );
    }

    #[test]
    fn numeric_payload_display() {
        assert_eq!(TestError::VariantC(42).to_string(), "Error code 42");
    }

    #[test]
    fn implements_error_trait() {
        let err: &dyn std::error::Error = &TestError::VariantA;
        assert_eq!(err.to_string(), "something went wrong");
    }

    #[test]
    fn explicit_discriminant() {
        assert_eq!(CodedError::First as u32, 0);
        assert_eq!(CodedError::Second as u32, 1);
    }

    #[test]
    fn explicit_discriminant_display() {
        assert_eq!(CodedError::First.to_string(), "first error");
        assert_eq!(CodedError::Second.to_string(), "second error");
    }
}
