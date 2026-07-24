/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Lightweight bitflags trait and macro.

use std::ops::{BitAnd, BitOr};

/// A trait for bitflag types around any integer type (`u8`, `u16`, `u32`, etc.).
///
/// Implementors get implementations of [`BitOr`] and [`BitOrAssign`](std::ops::BitOrAssign)
/// for combining flags, and default [`any`](Bitflags::any),
/// [`all`](Bitflags::all), and [`empty`](Bitflags::empty) methods.
///
/// The associated type [`Bits`](Bitflags::Bits) can be any integer type that
/// supports the required bitwise operations (`u8`, `u16`, `u32`, etc.).
///
/// # Usage
///
/// Use the [`declare_bitflags!`](crate::declare_bitflags) macro to define a flags type:
///
/// ```ignore
/// declare_bitflags! {
///     #[derive(Debug, Clone, Copy, PartialEq, Eq)]
///     pub struct MyFlags: u32 {
///         const NONE = 0;
///         const READ = 1 << 0;
///         const WRITE = 1 << 1;
///         const EXECUTE = 1 << 2;
///     }
/// }
///
/// // combine flags with `|` and `|=`
/// let flags = MyFlags::READ | MyFlags::WRITE;
/// let mut flags2 = MyFlags::READ;
/// flags2 |= MyFlags::EXECUTE;
///
/// // check whether all specified flags are set
/// assert!(flags.all(MyFlags::READ));
/// assert!(flags.all(MyFlags::READ | MyFlags::WRITE));
/// assert!(!flags.all(MyFlags::READ | MyFlags::EXECUTE));
///
/// // check whether any of the specified flags are set
/// assert!(flags.any(MyFlags::READ | MyFlags::EXECUTE));
/// assert!(!flags.any(MyFlags::EXECUTE));
///
/// // access the raw bits
/// assert_eq!(flags.bits(), 0b011);
/// assert_eq!(MyFlags::from_bits(0b011), flags);
///
/// // create an empty flags value
/// let empty = MyFlags::empty();
/// assert!(!empty.any(MyFlags::READ));
/// ```
pub trait Bitflags: Copy
where
    Self::Bits:
        Copy + Default + BitAnd<Output = Self::Bits> + BitOr<Output = Self::Bits> + PartialEq,
{
    /// The underlying integer type (e.g. `u8`, `u16`, `u32`).
    type Bits;

    /// Returns the raw bits.
    ///
    /// ```ignore
    /// let flags = MyFlags::READ | MyFlags::WRITE;
    /// assert_eq!(flags.bits(), 0b11);
    /// ```
    fn bits(self) -> Self::Bits;

    /// Creates a flags value from raw bits.
    ///
    /// Bits that do not correspond to any defined flag are silently cleared.
    ///
    /// ```ignore
    /// let flags = MyFlags::from_bits(0b01);
    /// assert_eq!(flags, MyFlags::READ);
    ///
    /// // undefined bits are masked off
    /// let flags = MyFlags::from_bits(0xFF);
    /// assert_eq!(flags.bits(), 0b111);
    /// ```
    fn from_bits(bits: Self::Bits) -> Self;

    /// Returns a flags value with no bits set.
    ///
    /// ```ignore
    /// let flags = MyFlags::empty();
    /// assert!(!flags.any(MyFlags::READ));
    /// ```
    fn empty() -> Self {
        Self::from_bits(Self::Bits::default())
    }

    /// Returns `true` if no bits are set.
    ///
    /// ```ignore
    /// assert!(MyFlags::NONE.is_empty());
    /// assert!(!MyFlags::READ.is_empty());
    /// ```
    fn is_empty(self) -> bool {
        self.bits() == Self::Bits::default()
    }

    /// Returns `true` if any of the flags in `other` are set in `self`.
    ///
    /// ```ignore
    /// let flags = MyFlags::READ | MyFlags::WRITE;
    /// assert!(flags.any(MyFlags::READ));
    /// assert!(flags.any(MyFlags::READ | MyFlags::EXECUTE));
    /// assert!(!flags.any(MyFlags::EXECUTE));
    /// ```
    fn any(self, other: Self) -> bool {
        self.bits() & other.bits() != Self::Bits::default()
    }

    /// Returns `true` if all of the flags in `other` are set in `self`.
    ///
    /// ```ignore
    /// let flags = MyFlags::READ | MyFlags::WRITE;
    /// assert!(flags.all(MyFlags::READ));
    /// assert!(flags.all(MyFlags::READ | MyFlags::WRITE));
    /// assert!(!flags.all(MyFlags::READ | MyFlags::EXECUTE));
    /// ```
    fn all(self, other: Self) -> bool {
        self.bits() & other.bits() == other.bits()
    }
}

/// Defines a bitflag struct and implements [`Bitflags`], [`BitOr`](std::ops::BitOr), and
/// [`BitOrAssign`](std::ops::BitOrAssign) for it.
///
/// The generated struct is a newtype over the specified integer type with
/// associated constants for each flag.
///
/// # Example
///
/// ```ignore
/// declare_bitflags! {
///     /// Flags controlling output formatting.
///     #[derive(Debug, Clone, Copy, PartialEq, Eq)]
///     pub struct FormatFlags: u8 {
///         /// No formatting.
///         const NONE = 0;
///         /// Pretty-print with indentation.
///         const INDENT = 1 << 0;
///     }
/// }
/// ```
#[macro_export]
macro_rules! declare_bitflags {
    (
        $(#[$outer:meta])*
        $vis:vis struct $Name:ident : $T:ty {
            $(
                $(#[$inner:meta])*
                const $FLAG:ident = $value:expr;
            )*
        }
    ) => {
        $(#[$outer])*
        $vis struct $Name($T);

        impl $Name {
            /// Mask of all valid flag bits.
            const VALID_MASK: $T = $( $value )|*;

            $(
                $(#[$inner])*
                $vis const $FLAG: Self = Self($value);
            )*
        }

        impl $crate::Bitflags for $Name {
            type Bits = $T;
            fn bits(self) -> $T { self.0 }
            fn from_bits(bits: $T) -> Self { Self(bits & Self::VALID_MASK) }
        }

        impl ::core::ops::BitOr for $Name {
            type Output = Self;
            fn bitor(self, rhs: Self) -> Self {
                use $crate::Bitflags;
                Self::from_bits(self.bits() | rhs.bits())
            }
        }

        impl ::core::ops::BitOrAssign for $Name {
            fn bitor_assign(&mut self, rhs: Self) {
                use $crate::Bitflags;
                *self = Self::from_bits(self.bits() | rhs.bits());
            }
        }
    };
}
