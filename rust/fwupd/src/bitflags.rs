/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Lightweight two-type bitflags: a flag enum for single values, a bitmask struct
//! for combined sets. This is split across two types to let the compiler enforce
//! APIs that only take one bitflag (as opposed to a multi-flag mask).
//!
//! Use the [`declare_bitflags!`](crate::declare_bitflags) macro to define a matched
//! pair of flag enum and bitmask struct.

use std::ops::{BitAnd, BitOr};

/// A trait for bitmask types that combine individual flag values.
///
/// Bitmask types come as a pair: an enum representing the individual flags
/// and a struct for the mask of multiple flags. The split is intentional to
/// allow the compiler to enforce APIs that take only a single flag value
/// but not a mask.
///
/// The mask is a newtype over an integer type ([`u8`], [`u16`], [`u32`], etc.),
/// it supports the usual operations one would expect from a bitmask.
///
/// # Usage
///
/// Use the [`declare_bitflags!`](crate::declare_bitflags) macro to define a
/// flag enum and bitmask struct pair:
///
/// ```ignore
/// declare_bitflags! {
///     #[derive(Debug, Clone, Copy, PartialEq, Eq)]
///     pub struct MyFlags / MyFlag : u32 {
///         Read = 1 << 0;
///         Write = 1 << 1;
///         Execute = 1 << 2;
///     }
/// }
///
/// // convert a single flag to a bitmask
/// let mask = MyFlag::Read.as_mask();
/// assert!(mask.contains(MyFlag::Read));
///
/// // combine flags with `|`
/// let mask = MyFlag::Read | MyFlag::Write;
///
/// // check whether a single flag is set
/// assert!(mask.contains(MyFlag::Read));
/// assert!(!mask.contains(MyFlag::Execute));
///
/// // check whether any/all of several flags are set
/// assert!(mask.any(MyFlag::Read | MyFlag::Execute));
/// assert!(mask.all(MyFlag::Read | MyFlag::Write));
/// assert!(!mask.all(MyFlag::Read | MyFlag::Execute));
///
/// // iterate over set flags
/// let flags: Vec<MyFlag> = mask.iter().collect();
/// assert_eq!(flags, vec![MyFlag::Read, MyFlag::Write]);
///
/// // access the raw bits
/// assert_eq!(mask.bits(), 0b011);
///
/// // create a bitmask from raw bits (undefined bits are masked off)
/// let mask = MyFlags::from_bits(0b011);
/// assert!(mask.contains(MyFlag::Read));
/// assert!(mask.contains(MyFlag::Write));
/// let mask = MyFlags::from_bits(0xFF);
/// assert_eq!(mask.bits(), 0b111);
///
/// // create an empty bitmask
/// let empty = MyFlags::empty();
/// assert!(empty.is_empty());
/// ```
///
/// A flag value may have multiple bits set and/or be a combination of
/// other flag values:
/// ```ignore
/// declare_bitflags! {
///     #[derive(Debug, Clone, Copy, PartialEq, Eq)]
///     pub struct MyFlags / MyFlag : u32 {
///         Read = 0x1;
///         Write = 0x2;
///         Execute = 0x4;
///         // Multiple bits and an alias to two other flags
///         ReadWrite = 0x3;
///     }
/// }
/// ```
/// All APIs except [`Bitflags::iter()`] work on the underlying
/// bit field values. Check the documentation for [`Bitflags::iter()`] to
/// avoid surprises.
pub trait Bitflags: Copy
where
    Self::Bits:
        Copy + Default + BitAnd<Output = Self::Bits> + BitOr<Output = Self::Bits> + PartialEq,
{
    /// The underlying integer type (e.g. [`u8`], [`u16`], [`u32`]).
    type Bits;

    /// The enum type for the allowed flags in this mask.
    type Flag: Copy + 'static;

    /// The defined flag values.
    const FLAGS: &'static [Self::Flag];

    /// Returns the raw bits as the underlying type (e.g. [`u8`], [`u16`], [`u32`]).
    fn bits(self) -> Self::Bits;

    /// Creates a bitmask from raw bits of the underlying type (e.g. [`u8`], [`u16`], [`u32`]).
    ///
    /// <div class="warning">
    /// Bits that do not correspond to any defined flag are silently cleared.
    /// </div>
    fn from_bits(bits: Self::Bits) -> Self;

    /// Returns the bits for a single flag value.
    ///
    /// There is no reason to use this function, it exists for Rust-internal
    /// implementation requirements.
    fn flag_bits(flag: Self::Flag) -> Self::Bits;

    /// Returns a bitmask with no bits set.
    #[must_use]
    fn empty() -> Self {
        Self::from_bits(Self::Bits::default())
    }

    /// Returns `true` if no bits are set.
    fn is_empty(self) -> bool {
        self.bits() == Self::Bits::default()
    }

    /// Returns `true` if all bits of the given flag are set.
    ///
    /// ```ignore
    /// let mask = MyFlag::Read | MyFlag::Write;
    /// assert!(mask.contains(MyFlag::Read));
    /// assert!(!mask.contains(MyFlag::Execute));
    /// ```
    /// This function operates on the underlying bits, a flag
    /// may have multiple bits set:
    /// ```ignore
    /// let mask = MyFlag::ReadWrite;
    /// assert!(mask.contains(MyFlag::Read));
    /// assert!(mask.contains(MyFlag::Write));
    /// assert!(mask.contains(MyFlag::ReadWrite));
    /// ```
    fn contains(self, flag: Self::Flag) -> bool {
        let flag_bits = Self::flag_bits(flag);
        self.bits() & flag_bits == flag_bits
    }

    /// Returns `true` if any of the flags in `other` are set in `self`.
    ///
    /// This function operates on the underlying bits, a flag
    /// is set if any of the flag's bits are set.
    ///
    /// ```ignore
    /// let mask = MyFlag::Read | MyFlag::Write;
    /// assert!(mask.any(MyFlag::Read | MyFlag::Execute));
    /// assert!(!mask.any(MyFlag::Execute.as_mask()));
    ///
    /// // A single bit of a multi-bit flag is enough
    /// let mask = MyFlag::Read.as_mask();
    /// assert!(mask.any(MyFlag::ReadWrite.as_mask()));
    /// ```
    fn any(self, other: Self) -> bool {
        self.bits() & other.bits() != Self::Bits::default()
    }

    /// Returns `true` if all of the flags in `other` are set in `self`.
    ///
    /// This function operates on the underlying bits, a flag
    /// is set if all of the flag's bits are set.
    ///
    /// ```ignore
    /// let mask = MyFlag::Read | MyFlag::Write;
    /// assert!(mask.all(MyFlag::Read | MyFlag::Write));
    /// assert!(!mask.all(MyFlag::Read | MyFlag::Execute));
    /// ```
    fn all(self, other: Self) -> bool {
        self.bits() & other.bits() == other.bits()
    }

    /// Returns an iterator over the individual flags set in this bitmask.
    ///
    /// Yields each defined flag whose bit is set, **in declaration order**.
    ///
    /// ```ignore
    /// let mask = MyFlag::Read | MyFlag::Execute;
    /// let flags: Vec<MyFlag> = mask.iter().collect();
    /// assert_eq!(flags, vec![MyFlag::Read, MyFlag::Execute]);
    /// ```
    /// Care must be taken for flags that alias other flags as they may
    /// yield flags not explicitly set:
    ///
    /// ```ignore
    /// let mask = MyFlag::ReadWrite | MyFlag::Write;
    /// let flags: Vec<MyFlag> = mask.iter().collect();
    /// // The Read flag is present even though it was not explicitly set
    /// assert_eq!(flags, vec![MyFlag::Read, MyFlag::Write, MyFlag::ReadWrite]);
    ///
    /// let mask = MyFlag::Read | MyFlag::Write;
    /// let flags: Vec<MyFlag> = mask.iter().collect();
    /// // The ReadWrite flag is present even though it was not explicitly set
    /// assert_eq!(flags, vec![MyFlag::Read, MyFlag::Write, MyFlag::ReadWrite]);
    /// ```
    fn iter(self) -> BitflagIter<Self> {
        BitflagIter {
            source: self,
            index: 0,
        }
    }
}

/// An iterator over the individual flags set in a [`Bitflags`].
///
/// Yields each defined flag whose bit is set, **in declaration order**.
pub struct BitflagIter<B: Bitflags>
where
    B::Bits: Copy + Default + BitAnd<Output = B::Bits> + BitOr<Output = B::Bits> + PartialEq,
{
    source: B,
    index: usize,
}

impl<B: Bitflags> Iterator for BitflagIter<B>
where
    B::Bits: Copy + Default + BitAnd<Output = B::Bits> + BitOr<Output = B::Bits> + PartialEq,
{
    type Item = B::Flag;

    fn next(&mut self) -> Option<Self::Item> {
        while self.index < B::FLAGS.len() {
            let flag = B::FLAGS[self.index];
            self.index += 1;

            let flag_bits = B::flag_bits(flag);
            if self.source.bits() & flag_bits == flag_bits {
                return Some(flag);
            }
        }
        None
    }
}

/// Defines a pair for handling bitflags: an enum representing the individual flags
/// and a struct for the mask of multiple flags. The mask implements [`Bitflags`] and
/// the necessary operator traits.
///
/// Each `const` entry becomes an enum variant with the specified bit value; entries
/// are not limited to single bit.
/// All values must be non-zero (use [`Bitflags::empty()`] for a zero-valued
/// bitmask).
///
/// ```ignore
/// declare_bitflags! {
///     /// A bitmask of file access permissions.
///     #[derive(Debug, Clone, Copy, PartialEq, Eq)]
///     pub struct MyFlags / MyFlag : u32 {
///         /// Read permission.
///         Read = 1 << 0;
///         /// Write permission.
///         Write = 1 << 1;
///         /// Execute permission.
///         Execute = 1 << 2;
///     }
/// }
/// ```
///
/// See [`Bitflags`] for usage examples and further details.
#[macro_export]
macro_rules! declare_bitflags {
    (
        $(#[$outer:meta])*
        $vis:vis struct $Mask:ident / $Flag:ident : $T:ty {
            $(
                $(#[$inner:meta])*
                $NAME:ident = $value:expr;
            )*
        }
    ) => {
        // -- Flag enum --
        #[doc = concat!("Individual flags for [`", stringify!($Mask), "`].")]
        #[allow(non_camel_case_types)]
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        $vis enum $Flag {
            $(
                $(#[$inner])*
                $NAME,
            )*
        }

        impl $Flag {
            /// Returns the raw bit value of this flag.
            #[inline]
            $vis const fn bits(self) -> $T {
                match self {
                    $(
                        $Flag::$NAME => $value,
                    )*
                }
            }

            /// Converts this flag into a bitmask containing just this flag.
            ///
            /// Equivalent to `Into::<Mask>::into(self)` but callable in
            /// const and method-chain contexts.
            ///
            /// ```ignore
            /// let mask = MyFlag::Read.as_mask();
            /// assert!(mask.contains(MyFlag::Read));
            /// assert!(!mask.contains(MyFlag::Write));
            /// ```
            #[inline]
            $vis const fn as_mask(self) -> $Mask {
                $Mask(self.bits())
            }
        }

        // -- Bitflags struct --
        $(#[$outer])*
        $vis struct $Mask($T);

        impl $Mask {
            /// Mask of all valid flag bits.
            const VALID_MASK: $T = $( $value )|*;
        }

        impl $crate::Bitflags for $Mask {
            type Bits = $T;
            type Flag = $Flag;

            const FLAGS: &'static [$Flag] = &[
                $( $Flag::$NAME, )*
            ];

            #[inline]
            fn bits(self) -> $T { self.0 }
            #[inline]
            fn from_bits(bits: $T) -> Self { Self(bits & Self::VALID_MASK) }
            #[inline]
            fn flag_bits(flag: $Flag) -> $T { flag.bits() }
        }

        // -- From<Flag> for Mask --
        impl ::core::convert::From<$Flag> for $Mask {
            #[inline]
            fn from(flag: $Flag) -> Self {
                use $crate::Bitflags;
                Self::from_bits(flag.bits())
            }
        }

        // -- Flag | Flag -> Mask --
        impl ::core::ops::BitOr for $Flag {
            type Output = $Mask;
            fn bitor(self, rhs: Self) -> $Mask {
                use $crate::Bitflags;
                $Mask::from_bits(self.bits() | rhs.bits())
            }
        }

        // -- Mask | Flag -> Mask --
        impl ::core::ops::BitOr<$Flag> for $Mask {
            type Output = Self;
            fn bitor(self, rhs: $Flag) -> Self {
                use $crate::Bitflags;
                Self::from_bits(self.bits() | rhs.bits())
            }
        }

        // -- Flag | Mask -> Mask --
        impl ::core::ops::BitOr<$Mask> for $Flag {
            type Output = $Mask;
            fn bitor(self, rhs: $Mask) -> $Mask {
                use $crate::Bitflags;
                $Mask::from_bits(self.bits() | rhs.bits())
            }
        }

        // -- Mask | Mask -> Mask --
        impl ::core::ops::BitOr for $Mask {
            type Output = Self;
            fn bitor(self, rhs: Self) -> Self {
                use $crate::Bitflags;
                Self::from_bits(self.bits() | rhs.bits())
            }
        }

        // -- Mask |= Flag --
        impl ::core::ops::BitOrAssign<$Flag> for $Mask {
            fn bitor_assign(&mut self, rhs: $Flag) {
                use $crate::Bitflags;
                *self = Self::from_bits(self.bits() | rhs.bits());
            }
        }

        // -- Mask |= Mask --
        impl ::core::ops::BitOrAssign for $Mask {
            fn bitor_assign(&mut self, rhs: Self) {
                use $crate::Bitflags;
                *self = Self::from_bits(self.bits() | rhs.bits());
            }
        }
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    declare_bitflags! {
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub struct TestFlags / TestFlag : u32 {
            Read = 1 << 0;
            Write = 1 << 1;
            Execute = 1 << 2;

            ReadWrite = 0x3;
        }
    }

    #[test]
    fn single_flag_into_mask() {
        let mask: TestFlags = TestFlag::Read.into();
        assert!(mask.contains(TestFlag::Read));
        assert!(!mask.contains(TestFlag::Write));
    }

    #[test]
    fn as_mask() {
        let mask = TestFlag::Write.as_mask();
        assert!(mask.contains(TestFlag::Write));
        assert!(!mask.contains(TestFlag::Read));
        assert_eq!(mask.bits(), TestFlag::Write.bits());
    }

    #[test]
    fn aliases() {
        let mask = TestFlag::ReadWrite.as_mask();
        assert!(mask.contains(TestFlag::Read));
        assert!(mask.contains(TestFlag::Write));
        assert!(!mask.contains(TestFlag::Execute));
    }

    #[test]
    fn combine_flags() {
        let mask = TestFlag::Read | TestFlag::Write;
        assert!(mask.contains(TestFlag::Read));
        assert!(mask.contains(TestFlag::Write));
        assert!(!mask.contains(TestFlag::Execute));

        let mask = TestFlag::ReadWrite | TestFlag::Write;
        assert!(mask.contains(TestFlag::Read));
        assert!(mask.contains(TestFlag::Write));
        assert!(!mask.contains(TestFlag::Execute));
    }

    #[test]
    fn bitor_assign_flag() {
        let mut mask = TestFlags::empty();
        mask |= TestFlag::Read;
        mask |= TestFlag::Execute;
        assert!(mask.contains(TestFlag::Read));
        assert!(!mask.contains(TestFlag::Write));
        assert!(mask.contains(TestFlag::Execute));
    }

    #[test]
    fn mask_or_flag() {
        let mask = TestFlag::Read | TestFlag::Write;
        let mask2 = mask | TestFlag::Execute;
        assert!(mask2.contains(TestFlag::Execute));
    }

    #[test]
    fn flag_or_mask() {
        let mask = TestFlag::Read | TestFlag::Write;
        let mask2 = TestFlag::Execute | mask;
        assert!(mask2.contains(TestFlag::Read));
        assert!(mask2.contains(TestFlag::Execute));
    }

    #[test]
    fn any_flags() {
        let mask = TestFlag::Read | TestFlag::Write;
        assert!(mask.any(TestFlag::Read | TestFlag::Execute));
        assert!(!mask.any(TestFlag::Execute.into()));

        let mask = TestFlag::Read.as_mask();
        assert!(mask.any(TestFlag::ReadWrite.as_mask()));

        let mask = TestFlag::ReadWrite.as_mask();
        assert!(mask.any(TestFlag::Read | TestFlag::Execute));
        assert!(!mask.any(TestFlag::Execute.into()));
    }

    #[test]
    fn all_flags() {
        let mask = TestFlag::Read | TestFlag::Write;
        assert!(mask.all(TestFlag::Read | TestFlag::Write));
        assert!(!mask.all(TestFlag::Read | TestFlag::Execute));

        let mask = TestFlag::ReadWrite.as_mask();
        assert!(mask.all(TestFlag::Read | TestFlag::Write));
        assert!(!mask.all(TestFlag::Read | TestFlag::Execute));
    }

    #[test]
    fn empty_and_is_empty() {
        let mask = TestFlags::empty();
        assert!(mask.is_empty());
        assert!(!mask.contains(TestFlag::Read));

        let mask2: TestFlags = TestFlag::Read.into();
        assert!(!mask2.is_empty());
    }

    #[test]
    fn from_bits_masks_invalid() {
        let mask = TestFlags::from_bits(0xFF);
        assert_eq!(mask.bits(), 0b111);
    }

    #[test]
    fn iter_single() {
        let mask: TestFlags = TestFlag::Write.into();
        let flags: Vec<TestFlag> = mask.iter().collect();
        assert_eq!(flags, vec![TestFlag::Write]);
    }

    #[test]
    fn iter_combined() {
        let mask = TestFlag::Read | TestFlag::Execute;
        let flags: Vec<TestFlag> = mask.iter().collect();
        assert_eq!(flags, vec![TestFlag::Read, TestFlag::Execute]);

        let mask = TestFlag::ReadWrite | TestFlag::Execute;
        let flags: Vec<TestFlag> = mask.iter().collect();
        assert_eq!(
            flags,
            vec![
                TestFlag::Read,
                TestFlag::Write,
                TestFlag::Execute,
                TestFlag::ReadWrite
            ]
        );
    }

    #[test]
    fn iter_all() {
        let mask = TestFlag::Read | TestFlag::Write | TestFlag::Execute;
        let flags: Vec<TestFlag> = mask.iter().collect();
        assert_eq!(
            flags,
            vec![
                TestFlag::Read,
                TestFlag::Write,
                TestFlag::Execute,
                TestFlag::ReadWrite
            ]
        );
    }

    #[test]
    fn iter_empty() {
        let mask = TestFlags::empty();
        let flags: Vec<TestFlag> = mask.iter().collect();
        assert!(flags.is_empty());
    }

    #[test]
    fn mask_or_mask() {
        let m1 = TestFlag::Read | TestFlag::Write;
        let m2: TestFlags = TestFlag::Execute.into();
        let m3 = m1 | m2;
        assert!(m3.contains(TestFlag::Read));
        assert!(m3.contains(TestFlag::Write));
        assert!(m3.contains(TestFlag::Execute));
    }

    #[test]
    fn bitor_assign_mask() {
        let mut m1: TestFlags = TestFlag::Read.into();
        let m2 = TestFlag::Write | TestFlag::Execute;
        m1 |= m2;
        assert!(m1.contains(TestFlag::Read));
        assert!(m1.contains(TestFlag::Write));
        assert!(m1.contains(TestFlag::Execute));
    }
}
