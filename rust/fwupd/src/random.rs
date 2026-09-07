/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Random number generator helpers.
//!
//! This module provides a few convenience function for quick and
//! easy access to random numbers. The actual functionality is
//! provided by the underlying RNG implementation which may be
//! changed at any time.
//!
//! # Warning:
//!
//! Do not use this where there is a requirement for a cryptographically secure RNG.
//!
//! # Example:
//! ```
//! # use fwupd::random::{RandomAlphaNumeric, RandomValue, RandomValueRanged, RandomValue01, RandomFill};
//!
//! let random_number = u64::random();
//! let random_number_with_range = u32::random_in_range(0..=100);
//! let between_0_and_1 = f64::random01_inclusive();
//!
//! let random_string: String = String::random_alphanumeric(5);
//! assert_eq!(random_string.len(), 5);
//!
//! let mut buffer = vec![0u8; 16];
//! buffer.random_fill();
//! ```
//!
use std::ops::{Bound, RangeBounds};

use fastrand;

/// Generate of a random sequence of alphanumeric
/// characters (`[A-Za-z0-9]`).
///
/// # Example:
/// ```
/// # use fwupd::random::RandomAlphaNumeric;
///
/// let random_string = String::random_alphanumeric(5);
/// let random_data: Vec<u8> = Vec::random_alphanumeric(10);
/// ```
pub trait RandomAlphaNumeric<T> {
    /// Create a new String containing alphanumeric
    //  characters (`[A-Za-z0-9]`).
    fn random_alphanumeric(length: usize) -> T;
}

impl RandomAlphaNumeric<String> for String {
    /// Generate a String of length `length` filled with alphanumeric
    //  characters (`[A-Za-z0-9]`).
    fn random_alphanumeric(length: usize) -> String {
        (0..length).map(|_| fastrand::alphanumeric()).collect()
    }
}

impl RandomAlphaNumeric<Vec<u8>> for Vec<u8> {
    /// Generate a `Vec<u8>` of size `length` filled with alphanumeric
    //  characters (`[A-Za-z0-9]`).
    fn random_alphanumeric(length: usize) -> Vec<u8> {
        (0..length)
            .map(|_| fastrand::alphanumeric())
            .map(|c| u8::try_from(c).unwrap_or(b'?'))
            .collect()
    }
}

/// Trait for obtaining a random number from a given type.
pub trait RandomValue: Sized {
    /// Return a random number within the available type range, e.g.
    /// `[0..=std::u32::MAX]` for `u32`.
    ///
    /// For floating point values between 0 and 1 see the
    /// [`RandomValue01`] trait.
    ///
    /// # Example:
    /// ```
    /// # use fwupd::random::RandomValue;
    /// let random_number = u64::random();
    /// ```
    #[must_use]
    fn random() -> Self;
}

/// Trait for obtaining a random number from a given type, within
/// a given range.
pub trait RandomValueRanged {
    /// Return a random number within the available range.
    ///
    /// For floating point values between 0 and 1 see the
    /// [`RandomValue01`] trait.
    #[must_use]
    fn random_in_range(bounds: impl RangeBounds<Self>) -> Self;
}

/// Trait for obtaining a random number.
pub trait RandomValue01: Sized {
    /// Return a random number in the range `0..=1`.
    #[must_use]
    fn random01_inclusive() -> Self;

    /// Return a random number in the range `0..1`.
    #[must_use]
    fn random01_exclusive() -> Self;
}

macro_rules! impl_random_value {
    ($type:ty, $func:ident) => {
        impl RandomValue for $type {
            fn random() -> Self {
                fastrand::$func(..)
            }
        }
        impl RandomValueRanged for $type {
            fn random_in_range(range: impl RangeBounds<Self>) -> Self {
                fastrand::$func(range)
            }
        }
    };
}

impl_random_value!(u64, u64);
impl_random_value!(u32, u32);
impl_random_value!(u8, u8);

impl RandomValue01 for f64 {
    fn random01_inclusive() -> Self {
        fastrand::f64_inclusive()
    }
    fn random01_exclusive() -> Self {
        fastrand::f64()
    }
}

impl RandomValue01 for f32 {
    fn random01_inclusive() -> Self {
        fastrand::f32_inclusive()
    }
    fn random01_exclusive() -> Self {
        fastrand::f32()
    }
}

impl RandomValue for bool {
    /// Returns a random `true` or `false`.
    fn random() -> Self {
        fastrand::bool()
    }
}

/// Provides a generic way of filling
/// a type with random data.
pub trait RandomFill {
    /// Fill the entire type with random values.
    fn random_fill(&mut self);

    /// Fill a range within the type with random values.
    fn random_fill_range(&mut self, range: impl RangeBounds<usize>);
}

impl<T: AsMut<[u8]>> RandomFill for T {
    fn random_fill(&mut self) {
        fastrand::fill(self.as_mut());
    }

    fn random_fill_range(&mut self, range: impl RangeBounds<usize>) {
        let slice = self.as_mut();
        let len = slice.len();

        let start = match range.start_bound() {
            Bound::Included(&n) => n,
            Bound::Excluded(&n) => n + 1,
            Bound::Unbounded => 0,
        };

        let end = match range.end_bound() {
            Bound::Included(&n) => n + 1,
            Bound::Excluded(&n) => n,
            Bound::Unbounded => len,
        };

        fastrand::fill(&mut slice[start..end]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[allow(unused_comparisons)]
    fn test_integers() {
        let r = u64::random();
        assert!((0..=u64::MAX).contains(&r)); // useless, I know
        let r = u32::random();
        assert!((0..=u32::MAX).contains(&r)); // useless, I know
        let r = u8::random();
        assert!((0..=u8::MAX).contains(&r)); // useless, I know

        let r = u64::random_in_range(0..=4);
        assert!((0..=4).contains(&r));
        let r = u32::random_in_range(0..=4);
        assert!((0..=4).contains(&r));
        let r = u8::random_in_range(0..=4);
        assert!((0..=4).contains(&r));

        let r = u64::random_in_range(0..4);
        assert!((0..4).contains(&r));
        let r = u32::random_in_range(0..4);
        assert!((0..4).contains(&r));
        let r = u8::random_in_range(0..4);
        assert!((0..4).contains(&r));
    }

    #[test]
    fn test_floats() {
        let r = f64::random01_inclusive();
        assert!((0.0..=1.00).contains(&r));
        let r = f32::random01_inclusive();
        assert!((0.0..=1.00).contains(&r));

        let r = f64::random01_exclusive();
        assert!((0.0..1.00).contains(&r));
        let r = f32::random01_exclusive();
        assert!((0.0..1.00).contains(&r));
    }

    #[test]
    fn test_alnums() {
        let s = String::random_alphanumeric(5);
        assert_eq!(s.len(), 5);

        let data: Vec<u8> = Vec::random_alphanumeric(10);
        assert_eq!(data.len(), 10);

        for byte in &data {
            match byte {
                b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' => {}
                _ => panic!("Invalid character {byte}"),
            }
        }
    }

    #[test]
    fn test_fill() {
        let mut data = [0u8; 10];
        data.random_fill();
        // Possible but rather unlikely that all bytes are still 0
        assert!(data.iter().any(|&b| b != 0));

        // Test with Vec<u8>
        let mut vec = vec![0u8; 20];
        vec.random_fill();
        assert!(vec.iter().any(|&b| b != 0));

        let mut data = [0u8; 20];
        data.random_fill_range(5..15);
        assert_eq!(&data[0..5], &[0, 0, 0, 0, 0]);
        assert_eq!(&data[15..20], &[0, 0, 0, 0, 0]);
        // You'd expect at least one of them to be not 0,
        // even with Dilbert's RNG
        assert!(data[5..15].iter().any(|&b| b != 0));

        let mut data = [0u8; 10];
        data.random_fill_range(2..=7);
        assert_eq!(&data[0..2], &[0, 0]);
        assert_eq!(&data[8..10], &[0, 0]);
        assert!(data[2..=7].iter().any(|&b| b != 0));
    }
}
