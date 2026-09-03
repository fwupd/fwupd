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
//! # use fwupd::units::{Bytes, KB, MB};
//! let sz: usize = KB(32).in_bytes();
//! let max_allocation_size: usize = MB(2).in_bytes();
//! ```

/// The number of bytes in a [KB], 1,024 bytes
const KB_BYTES: usize = 1 << 10;

/// The number of bytes in a [MB], 1,048,576 bytes
const MB_BYTES: usize = 1 << 20;

/// The number of bytes in a [GB], 1,073,741,824 bytes
const GB_BYTES: usize = 1 << 30;

/// Unit representing a number of Kilobytes (KB, 1024 bytes, technically KiB).
///
/// This unit represents an exact number of KB, not a partial one.
/// Use normal `usize` bytes if you need partial KB.
///
/// The primary use of this struct is to improve readability, e.g. to allocate
/// a 32 KB vector:
/// ```
/// # use fwupd::units::{Bytes, KB};
/// let v = vec![0; KB(32).in_bytes()];
/// ```
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub struct KB(pub usize);

/// Unit representing a number of exact Megabytes (MB, 1024 * 1024 bytes, technically MiB).
///
/// This unit represents an exact number of MB, not a partial one.
/// Use normal `usize` bytes or [KB] if you need partial MB.
///
/// The primary use of this struct is to improve readability, e.g. to allocate
/// a 2 MB vector:
/// ```
/// # use fwupd::units::{Bytes, MB};
/// let v = vec![0; MB(2).in_bytes()];
/// ```
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub struct MB(pub usize);

/// Unit representing Gigabytes (GB, 1024 * 1024 * 1024 bytes, technically GiB).
///
/// This unit represents an exact number of GB, not a partial one.
/// Use normal `usize` bytes, [KB] or [MB] if you need partial GB.
///
/// The primary use of this struct is to improve readability, e.g. to allocate
/// the length  1 GB vector:
/// ```ignore
/// # use fwupd::units::{Bytes, GB};
/// let v = vec![0; GB(1).in_bytes()];
/// ```
///
/// # Warning
///
/// For convenience [GB] contains a [usize]. On 32-bit builds using [GB] thus
/// has a natural limit of 4 (if converting to bytes). No checks are in place
/// to ensure this - it shouldn't ever matter for real code.
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub struct GB(pub usize);

/// Conversion trait between [KB], [MB] and [GB]
pub trait Bytes {
    /// Returns the number of bytes represented by this unit
    #[must_use]
    fn in_bytes(&self) -> usize;

    /// Returns the minimum number of kilobytes ([KB]) represented by this unit,
    /// rounded up so that the number of bytes will fit, e.g. 1025 bytes require
    /// 2 KB space.
    #[must_use]
    fn in_kb(&self) -> usize {
        self.as_kb().0
    }

    /// Returns the minimum number of megabytes ([MB]) represented by this unit,
    /// rounded up so that the number of bytes will fit, e.g. 1025 kilobytes
    /// require 2 MB space.
    #[must_use]
    fn in_mb(&self) -> usize {
        self.as_mb().0
    }

    /// Returns the minimum number of gigabytes ([GB]) represented by this unit,
    /// rounded up so that the number of bytes will fit, e.g. 1025 megabytes
    /// require 2 GB space.
    #[must_use]
    fn in_gb(&self) -> usize {
        self.as_gb().0
    }

    /// Returns the minimum number of kilobytes ([KB]) represented by this unit,
    /// rounded up so that the number of bytes will fit, e.g. 1025 bytes require
    /// 2 KB space.
    #[must_use]
    fn as_kb(&self) -> KB {
        KB(self.in_bytes().div_ceil(KB_BYTES))
    }

    /// Returns the minimum number of megabytes ([MB]) represented by this unit,
    /// rounded up so that the number of bytes will fit, e.g. 1025 kilobytes
    /// require 2 MB space.
    #[must_use]
    fn as_mb(&self) -> MB {
        MB(self.in_bytes().div_ceil(MB_BYTES))
    }

    /// Returns the minimum number of gigabytes ([GB]) represented by this unit,
    /// rounded up so that the number of bytes will fit, e.g. 1025 megabytes
    /// require 2 GB space.
    #[must_use]
    fn as_gb(&self) -> GB {
        GB(self.in_bytes().div_ceil(GB_BYTES))
    }
}

impl Bytes for KB {
    fn in_bytes(&self) -> usize {
        self.0 * KB_BYTES
    }
}

impl Bytes for MB {
    fn in_bytes(&self) -> usize {
        self.0 * MB_BYTES
    }
}

impl Bytes for GB {
    fn in_bytes(&self) -> usize {
        self.0 * GB_BYTES
    }
}

impl From<usize> for KB {
    /// Convert to KB, returning the number of KBs required to
    /// contain the given byte count.
    fn from(bytes: usize) -> KB {
        KB(bytes.div_ceil(KB_BYTES))
    }
}

impl From<usize> for MB {
    /// Convert to MB, returning the number of MBs required to
    /// contain the given byte count.
    fn from(bytes: usize) -> MB {
        MB(bytes.div_ceil(MB_BYTES))
    }
}

impl From<usize> for GB {
    /// Convert to MB, returning the number of GBs required to
    /// contain the given byte count.
    fn from(bytes: usize) -> GB {
        GB(bytes.div_ceil(GB_BYTES))
    }
}

impl KB {
    /// Convert the given byte count to the minimum number of [KB] required,
    /// e.g. a byte count of 1025 returns 2 KB.
    #[must_use]
    pub fn from_bytes(number_of_bytes: usize) -> Self {
        Self(number_of_bytes.div_ceil(KB_BYTES))
    }
}

impl MB {
    /// Convert the given byte count to the minimum number of [MB] required,
    /// e.g. a byte count of 1025 * 1024 returns 2 MB.
    #[must_use]
    pub fn from_bytes(number_of_bytes: usize) -> MB {
        Self(number_of_bytes.div_ceil(MB_BYTES))
    }
}

impl GB {
    /// Convert the given byte count to the minimum number of [MB] required,
    /// e.g. a byte count of 1025 * 1024 * 1024 returns 2 GB.
    #[must_use]
    pub fn from_bytes(number_of_bytes: usize) -> GB {
        Self(number_of_bytes.div_ceil(GB_BYTES))
    }
}

impl std::fmt::Display for KB {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{}KB", self.0)
    }
}

impl std::fmt::Display for MB {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{}MB", self.0)
    }
}

impl std::fmt::Display for GB {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{}GB", self.0)
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
    fn test_kb() {
        // in_bytes
        assert_eq!(KB(1).in_bytes(), 1024);
        assert_eq!(KB(2).in_bytes(), 2048);
        assert_eq!(KB(1024).in_bytes(), 1_048_576);

        // from_bytes with exact values
        assert_eq!(KB::from_bytes(1024), KB(1));
        assert_eq!(KB::from_bytes(2048), KB(2));
        assert_eq!(KB::from_bytes(1_048_576), KB(1024));

        // from_bytes with rounding
        assert_eq!(KB::from_bytes(1025), KB(2)); // rounds up
        assert_eq!(KB::from_bytes(1023), KB(1)); // rounds up
        assert_eq!(KB::from_bytes(0), KB(0));

        // conversions from other units
        assert_eq!(KB(1).as_kb(), KB(1));
        assert_eq!(MB(1).as_kb(), KB(1024));
        assert_eq!(GB(1).as_kb(), KB(1024 * 1024));

        // conversions to other units
        assert_eq!(KB(1024).as_mb(), MB(1));
        assert_eq!(KB(2048).as_mb(), MB(2));
        assert_eq!(KB(1536).as_mb(), MB(2)); // rounds up
        assert_eq!(KB(1024 * 1024).as_gb(), GB(1));
    }

    #[test]
    fn test_mb() {
        // in_bytes
        assert_eq!(MB(1).in_bytes(), 1_048_576);
        assert_eq!(MB(2).in_bytes(), 2_097_152);
        assert_eq!(MB(1024).in_bytes(), 1_073_741_824);

        // from_bytes with exact values
        assert_eq!(MB::from_bytes(1_048_576), MB(1));
        assert_eq!(MB::from_bytes(2_097_152), MB(2));

        // from_bytes with rounding
        assert_eq!(MB::from_bytes(1_048_577), MB(2)); // rounds up
        assert_eq!(MB::from_bytes(1_048_575), MB(1)); // rounds up

        // conversions from other units
        assert_eq!(KB(1024).as_mb(), MB(1));
        assert_eq!(KB(2048).as_mb(), MB(2));
        assert_eq!(MB(1).as_mb(), MB(1));
        assert_eq!(GB(1).as_mb(), MB(1024));
        assert_eq!(GB(2).as_mb(), MB(2048));

        // conversions to other units
        assert_eq!(MB(1).as_kb(), KB(1024));
        assert_eq!(MB(2).as_kb(), KB(2048));
        assert_eq!(MB(1024).as_gb(), GB(1));
        assert_eq!(MB(2048).as_gb(), GB(2));
        assert_eq!(MB(1536).as_gb(), GB(2)); // rounds up
    }

    #[test]
    fn test_gb() {
        // in_bytes
        assert_eq!(GB(1).in_bytes(), 1_073_741_824);
        assert_eq!(GB(2).in_bytes(), 2_147_483_648);

        // from_bytes with exact values
        assert_eq!(GB::from_bytes(1_073_741_824), GB(1));
        assert_eq!(GB::from_bytes(2_147_483_648), GB(2));

        // from_bytes with rounding
        assert_eq!(GB::from_bytes(1_073_741_825), GB(2)); // rounds up
        assert_eq!(GB::from_bytes(1_073_741_823), GB(1)); // rounds up

        // conversions from other units
        assert_eq!(KB(1024 * 1024).as_gb(), GB(1));
        assert_eq!(MB(1024).as_gb(), GB(1));
        assert_eq!(MB(2048).as_gb(), GB(2));
        assert_eq!(GB(1).as_gb(), GB(1));

        // conversions to other units
        assert_eq!(GB(1).as_kb(), KB(1024 * 1024));
        assert_eq!(GB(1).as_mb(), MB(1024));
        assert_eq!(GB(2).as_mb(), MB(2048));
    }

    #[test]
    fn test_chain_conversions() {
        let gb = GB(2);
        let mb = gb.as_mb();
        assert_eq!(mb, MB(2048));
        let kb = mb.as_kb();
        assert_eq!(kb, KB(2048 * 1024));
        let bytes = kb.in_bytes();
        assert_eq!(bytes, 2 * GB_BYTES);
    }
}
