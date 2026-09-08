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
use std::io::ErrorKind;
use std::ops::{Bound, RangeBounds};

use getrandom;

/// Handle a RNG error
///
/// # Panics
///
/// This function panics, the [getrandom documentation](https://docs.rs/getrandom/latest/getrandom/#error-handling)
/// says:
/// > Generally, on supported platforms, failure is highly unlikely, though not impossible. If an
/// > error does occur, it is likely that it will occur on every call to getrandom. Therefore,
/// > after the first successful call, one can be reasonably confident that no errors will occur.
///
/// but it [also says](https://docs.rs/getrandom/latest/getrandom/fn.fill.html)
/// > This function returns an error on any failure, including partial reads. We make no
/// > guarantees regarding the contents of dest on error.
///
/// So ... we retry (indefinitely) for partial reads and interruptions and fail hard for
/// anything else, hard. This is unlikely to happen for us anyway.
fn handle_rng_error(e: getrandom::Error) {
    match e {
        getrandom::Error::UNSUPPORTED | getrandom::Error::UNEXPECTED => {
            panic!("RNG failed with {e}, unable to continue");
        }
        _ => {
            let ioerr = std::io::Error::from(e);
            match ioerr.kind() {
                ErrorKind::UnexpectedEof | ErrorKind::Interrupted | ErrorKind::WouldBlock => {}
                _ => panic!("RNG failed with {e}, unable to continue"),
            }
        }
    }
}

/// Return a random u64 number
///
/// # Panics
///
/// This function may panic, se [`handle_rng_error`] for details
fn random() -> u64 {
    loop {
        match getrandom::u64() {
            Ok(v) => return v,
            Err(e) => handle_rng_error(e),
        }
    }
}

/// Return a random alphanumeric character (`[A-Za-z0-9]`).
///
/// # Panics
///
/// This function may panic, se [`handle_rng_error`] for details
fn random_alphanumeric() -> u8 {
    loop {
        let byte = u8::try_from(random() & 0xff).expect("Cannot happen");
        match byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' => return byte,
            _ => {}
        }
    }
}

/// Fill the given `dest` with random data
///
/// # Panics
///
/// This function may panic, se [`handle_rng_error`] for details
fn random_fill(dest: &mut [u8]) {
    loop {
        match getrandom::fill(dest) {
            Ok(()) => return,
            Err(e) => handle_rng_error(e),
        }
    }
}

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
        (0..length)
            .map(|_| char::from(random_alphanumeric()))
            .collect()
    }
}

impl RandomAlphaNumeric<Vec<u8>> for Vec<u8> {
    /// Generate a `Vec<u8>` of size `length` filled with alphanumeric
    //  characters (`[A-Za-z0-9]`).
    fn random_alphanumeric(length: usize) -> Vec<u8> {
        (0..length).map(|_| random_alphanumeric()).collect()
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
                let r = random() & u64::from(Self::MAX);
                Self::try_from(r).unwrap_or(0)
            }
        }
        impl RandomValueRanged for $type {
            fn random_in_range(range: impl RangeBounds<Self>) -> Self {
                let start = match range.start_bound() {
                    Bound::Included(&n) => n,
                    Bound::Excluded(&n) => n.saturating_add(1),
                    Bound::Unbounded => 0,
                };

                let end = match range.end_bound() {
                    Bound::Included(&n) => n,
                    Bound::Excluded(&n) => n.saturating_sub(1),
                    Bound::Unbounded => Self::MAX,
                };

                if start >= end {
                    return start;
                }

                // Special case: full range [0, MAX] has no bias, accept any value
                if start == 0 && end == Self::MAX {
                    return Self::random();
                }

                let range_size = end - start + 1;
                // Bias zone: when the max value doesn't divide evenly by range_size,
                // using modulo causes bias. e.g. for u8 (0-255) with range 0..9 (size=10):
                // 256 % 10 = 6, so we have 6 extra mappings (250→0, 251→1, ... 255→5)
                // making 0-5 more likely than 6-9. We avoid this by simply ignoring
                // any value within that bias zone.
                let bias_zone = Self::MAX % range_size;
                let ignore_threshold = Self::MAX - bias_zone;
                loop {
                    let r = Self::random();
                    if r >= ignore_threshold {
                        continue;
                    }
                    return start + (r % range_size);
                }
            }
        }
    };
}

impl_random_value!(u64, u64);
impl_random_value!(u32, u32);
impl_random_value!(u8, u8);

macro_rules! impl_random_value01 {
    ($type:ty, $func:ident) => {
        impl RandomValue01 for $type {
            #[allow(clippy::cast_precision_loss)]
            fn random01_inclusive() -> Self {
                // random() gives us a u64, we want mantissa digits (md) bits of randomness,
                // then divide by 2^(md) - 1
                const SCALE: u64 = (1u64 << <$type>::MANTISSA_DIGITS) - 1u64;
                const SHIFT: usize = 64 - <$type>::MANTISSA_DIGITS as usize;
                ((random() >> SHIFT) as $type) / SCALE as $type
            }
            #[allow(clippy::cast_precision_loss)]
            fn random01_exclusive() -> Self {
                // random() gives us a u64, we want mantissa digits (md) bits of randomness,
                // then multiply by 2^-(md)
                const SCALE: $type = 1.0 / ((1u64 << <$type>::MANTISSA_DIGITS) as $type);
                const SHIFT: usize = 64 - <$type>::MANTISSA_DIGITS as usize;
                ((random() >> SHIFT) as $type) * SCALE
            }
        }
    };
}

impl_random_value01!(f64, f64);
impl_random_value01!(f32, f32);

impl RandomValue for bool {
    /// Returns a random `true` or `false`.
    fn random() -> Self {
        random() & 0x1 == 0x1
    }
}

/// Provides a generic way of filling
/// a type with random data.
pub trait RandomFill {
    /// Fill the entire type with random values.
    fn random_fill(&mut self);
}

impl<T: AsMut<[u8]>> RandomFill for T {
    fn random_fill(&mut self) {
        random_fill(self.as_mut());
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

        // Test with subslice
        let mut data = [0u8; 20];
        data[5..15].as_mut().random_fill();
        assert_eq!(&data[0..5], &[0, 0, 0, 0, 0]);
        assert_eq!(&data[15..20], &[0, 0, 0, 0, 0]);
        // You'd expect at least one of them to be not 0,
        // even with Dilbert's RNG
        assert!(data[5..15].iter().any(|&b| b != 0));
    }
}
