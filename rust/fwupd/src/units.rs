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
/// The primary use of this struct is to improve readability, e.g. to allocate
/// a 32 KB vector:
/// ```
/// # use fwupd::units::ByteCount;
/// let v = vec![0u8; ByteCount::from_kb(32).count()];
/// ```
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub struct ByteCount(usize);

impl ByteCount {
    #[must_use]
    pub fn count(self) -> usize {
        self.0
    }

    /// Initialize a `ByteCount` from a number of kilobytes
    #[must_use]
    pub fn from_kb(kb: usize) -> Self {
        Self(kb * KB_BYTES)
    }

    /// Initialize a `ByteCount` from a number of megabytes
    #[must_use]
    pub fn from_mb(mb: usize) -> Self {
        Self(mb * MB_BYTES)
    }

    /// Initialize a `ByteCount` from a number of gigabytes
    #[must_use]
    pub fn from_gb(gb: usize) -> Self {
        Self(gb * GB_BYTES)
    }

    /// Returns the minimum number of kilobytes represented by this `ByteCount`,
    /// rounded up so that the number of bytes will fit, e.g. 1025 bytes require
    /// 2 KB space.
    #[must_use]
    pub fn in_kb(&self) -> usize {
        self.count().div_ceil(KB_BYTES)
    }

    /// Returns the minimum number of megabytes represented by this `ByteCount`,
    /// rounded up so that the number of bytes will fit, e.g. 1025 kilobytes require
    /// 2 MB space.
    #[must_use]
    pub fn in_mb(&self) -> usize {
        self.count().div_ceil(MB_BYTES)
    }

    /// Returns the minimum number of gigabytes represented by this `ByteCount`,
    /// rounded up so that the number of bytes will fit, e.g. 1025 megabytes require
    /// 2 GB space.
    #[must_use]
    pub fn in_gb(&self) -> usize {
        self.count().div_ceil(GB_BYTES)
    }
}

impl From<usize> for ByteCount {
    /// Convert to a `ByteCount`
    fn from(bytes: usize) -> ByteCount {
        ByteCount(bytes)
    }
}

fn fmt_f64(value: f64) -> String {
    let s = format!("{value:.1}");
    s.trim_end_matches('0').trim_end_matches('.').to_string()
}

impl std::fmt::Display for ByteCount {
    #[allow(clippy::cast_precision_loss)]
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self.0 {
            gb if gb > GB_BYTES => write!(f, "{}GB", fmt_f64(self.0 as f64 / GB_BYTES as f64)),
            mb if mb > MB_BYTES => write!(f, "{}MB", fmt_f64(self.0 as f64 / MB_BYTES as f64)),
            kb if kb > KB_BYTES => write!(f, "{}KB", fmt_f64(self.0 as f64 / KB_BYTES as f64)),
            _ => write!(f, "{}B", self.0),
        }
    }
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
        assert_eq!(ByteCount::from_kb(1).count(), 1024);
        assert_eq!(ByteCount::from_kb(2).count(), 2048);
        assert_eq!(ByteCount::from_kb(1024).count(), MB_BYTES);
    }

    #[test]
    fn test_from_mb() {
        assert_eq!(ByteCount::from_mb(1).count(), MB_BYTES);
        assert_eq!(ByteCount::from_mb(2).count(), 2 * MB_BYTES);
        assert_eq!(ByteCount::from_mb(1024).count(), GB_BYTES);
    }

    #[test]
    fn test_from_gb() {
        assert_eq!(ByteCount::from_gb(1).count(), GB_BYTES);
        assert_eq!(ByteCount::from_gb(2).count(), 2 * GB_BYTES);
    }

    #[test]
    fn test_in_kb() {
        assert_eq!(ByteCount(0).in_kb(), 0);
        assert_eq!(ByteCount(1024).in_kb(), 1);
        assert_eq!(ByteCount(2048).in_kb(), 2);
        assert_eq!(ByteCount(1025).in_kb(), 2); // rounds up
        assert_eq!(ByteCount(1023).in_kb(), 1); // rounds up
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
        assert_eq!(format!("{}", ByteCount(0)), "0B");
        assert_eq!(format!("{}", ByteCount(512)), "512B");
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
