/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! This module contains unit types to make code more readable
//! and less prone to unit confusion errors.
//!
//! # Example:
//!
//! ```
//! # use fwupd::units::ByteCount;
//! let sz: usize = ByteCount::from_kb(32).count();
//! let max_allocation_size: usize = ByteCount::from_mb(2).count();
//! ```

/// The number of bytes in a [KB], 1,024 bytes
const KB_BYTES: usize = 1 << 10;

/// The number of bytes in a [MB], 1,048,576 bytes
const MB_BYTES: usize = 1 << 20;

/// The number of bytes in a [GB], 1,073,741,824 bytes
const GB_BYTES: usize = 1 << 30;

/// Unit representing an exact number of bytes.
///
/// This is only available for unsigned types at least 32-bits wide, i.e. `u32`,
/// `u64` and `usize`.
///
/// The primary use of this struct is to improve readability, e.g. to allocate
/// a 32 KB vector:
/// ```
/// # use fwupd::units::ByteCount;
/// let v = vec![0u8; ByteCount::from_kb(32).count()];
/// ```
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub struct ByteCount<T>(T);

/// Implement ByteCount for the given type
macro_rules! impl_byte_count {
    ($t:ty) => {
        impl ByteCount<$t> {
            const KB_BYTES: $t = 1 << 10;
            const MB_BYTES: $t = 1 << 20;
            const GB_BYTES: $t = 1 << 30;

            #[must_use]
            pub const fn count(self) -> $t {
                self.0
            }

            /// Initialize a `ByteCount` from a number of kilobytes
            #[must_use]
            pub const fn from_kb(kb: $t) -> Self {
                Self(kb * Self::KB_BYTES)
            }

            /// Initialize a `ByteCount` from a number of megabytes
            #[must_use]
            pub const fn from_mb(mb: $t) -> Self {
                Self(mb * Self::MB_BYTES)
            }

            /// Initialize a `ByteCount` from a number of gigabytes
            #[must_use]
            pub const fn from_gb(gb: $t) -> Self {
                Self(gb * Self::GB_BYTES)
            }

            /// Returns the minimum number of kilobytes represented by this `ByteCount`,
            /// rounded up so that the number of bytes will fit, e.g. 1025 bytes require
            /// 2 KB space.
            #[must_use]
            pub const fn in_kb(&self) -> $t {
                self.count().div_ceil(Self::KB_BYTES)
            }

            /// Returns the minimum number of megabytes represented by this `ByteCount`,
            /// rounded up so that the number of bytes will fit, e.g. 1025 kilobytes require
            /// 2 MB space.
            #[must_use]
            pub const fn in_mb(&self) -> $t {
                self.count().div_ceil(Self::MB_BYTES)
            }

            /// Returns the minimum number of gigabytes represented by this `ByteCount`,
            /// rounded up so that the number of bytes will fit, e.g. 1025 megabytes require
            /// 2 GB space.
            #[must_use]
            pub const fn in_gb(&self) -> $t {
                self.count().div_ceil(Self::GB_BYTES)
            }
        }
        impl From<$t> for ByteCount<$t> {
            /// Convert to a `ByteCount`
            fn from(bytes: $t) -> ByteCount<$t> {
                ByteCount(bytes)
            }
        }

        impl From<ByteCount<$t>> for f64 {
            fn from(bytes: ByteCount<$t>) -> f64 {
                bytes.0 as f64
            }
        }

        impl std::fmt::Display for ByteCount<$t> {
            #[allow(clippy::cast_precision_loss)]
            fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
                match self.0 {
                    gb if gb > Self::GB_BYTES => {
                        write!(f, "{}GB", fmt_f64(self.0 as f64 / Self::GB_BYTES as f64))
                    }
                    mb if mb > Self::MB_BYTES => {
                        write!(f, "{}MB", fmt_f64(self.0 as f64 / Self::MB_BYTES as f64))
                    }
                    kb if kb > Self::KB_BYTES => {
                        write!(f, "{}KB", fmt_f64(self.0 as f64 / Self::KB_BYTES as f64))
                    }
                    _ => write!(f, "{}B", self.0),
                }
            }
        }
    };
}

impl_byte_count!(u32);
impl_byte_count!(usize);
impl_byte_count!(u64);

fn fmt_f64(value: f64) -> String {
    let s = format!("{value:.1}");
    s.trim_end_matches('0').trim_end_matches('.').to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_constants() {
        assert_eq!(KB_BYTES, 1024);
        assert_eq!(MB_BYTES, 1_048_576);
        assert_eq!(GB_BYTES, 1_073_741_824);
    }

    #[test]
    fn test_from_kb() {
        assert_eq!(ByteCount::<usize>::from_kb(1).count(), 1024);
        assert_eq!(ByteCount::<usize>::from_kb(2).count(), 2048);
        assert_eq!(ByteCount::<usize>::from_kb(1024).count(), MB_BYTES);
    }

    #[test]
    fn test_from_mb() {
        assert_eq!(ByteCount::<usize>::from_mb(1).count(), MB_BYTES);
        assert_eq!(ByteCount::<usize>::from_mb(2).count(), 2 * MB_BYTES);
        assert_eq!(ByteCount::<usize>::from_mb(1024).count(), GB_BYTES);
    }

    #[test]
    fn test_from_gb() {
        assert_eq!(ByteCount::<usize>::from_gb(1).count(), GB_BYTES);
        assert_eq!(ByteCount::<usize>::from_gb(2).count(), 2 * GB_BYTES);
    }

    #[test]
    fn test_in_kb() {
        assert_eq!(ByteCount(0usize).in_kb(), 0);
        assert_eq!(ByteCount(1024usize).in_kb(), 1);
        assert_eq!(ByteCount(2048usize).in_kb(), 2);
        assert_eq!(ByteCount(1025usize).in_kb(), 2); // rounds up
        assert_eq!(ByteCount(1023usize).in_kb(), 1); // rounds up
    }

    #[test]
    fn test_in_mb() {
        assert_eq!(ByteCount(MB_BYTES).in_mb(), 1);
        assert_eq!(ByteCount(2 * MB_BYTES).in_mb(), 2);
        assert_eq!(ByteCount(MB_BYTES + 1).in_mb(), 2); // rounds up
        assert_eq!(ByteCount(MB_BYTES - 1).in_mb(), 1); // rounds up
    }

    #[test]
    fn test_in_gb() {
        assert_eq!(ByteCount(GB_BYTES).in_gb(), 1);
        assert_eq!(ByteCount(2 * GB_BYTES).in_gb(), 2);
        assert_eq!(ByteCount(GB_BYTES + 1).in_gb(), 2); // rounds up
        assert_eq!(ByteCount(GB_BYTES - 1).in_gb(), 1); // rounds up
    }

    #[test]
    fn test_from_usize() {
        assert_eq!(ByteCount::from(1024_usize), ByteCount(1024));
    }

    #[test]
    fn test_display() {
        assert_eq!(format!("{}", ByteCount(0usize)), "0B");
        assert_eq!(format!("{}", ByteCount(512usize)), "512B");
        assert_eq!(format!("{}", ByteCount(KB_BYTES)), "1024B");

        assert_eq!(format!("{}", ByteCount(KB_BYTES + 1)), "1KB");
        assert_eq!(format!("{}", ByteCount(2 * KB_BYTES)), "2KB");
        assert_eq!(format!("{}", ByteCount(MB_BYTES)), "1024KB");
        assert_eq!(format!("{}", ByteCount(KB_BYTES + KB_BYTES / 2)), "1.5KB");
        assert_eq!(format!("{}", ByteCount(KB_BYTES + KB_BYTES / 10)), "1.1KB");

        assert_eq!(format!("{}", ByteCount(MB_BYTES + 1)), "1MB");
        assert_eq!(format!("{}", ByteCount(2 * MB_BYTES)), "2MB");
        assert_eq!(format!("{}", ByteCount(GB_BYTES)), "1024MB");
        assert_eq!(format!("{}", ByteCount(MB_BYTES + MB_BYTES / 2)), "1.5MB");

        assert_eq!(format!("{}", ByteCount(GB_BYTES + 1)), "1GB");
        assert_eq!(format!("{}", ByteCount(2 * GB_BYTES)), "2GB");
        assert_eq!(format!("{}", ByteCount(GB_BYTES + GB_BYTES / 2)), "1.5GB");
    }
}
