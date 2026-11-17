/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Big-endian supplemental types to augment
//! [zerocopy](https://docs.rs/zerocopy/latest/zerocopy) types.

use zerocopy::{FromBytes, Immutable, IntoBytes, Unaligned};

/// 24-bit big endian integer.
///
/// Supplemental type for zerocopy types which lack a 24-bit type. For all other bit widths use
/// zerocopy types (e.g. [`zerocopy::byteorder::big_endian::U16`])
#[derive(FromBytes, Immutable, IntoBytes, Unaligned, Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C, packed)]
pub struct U24([u8; 3]);

impl U24 {
    /// Maximum value that can be stored in a 24-bit integer (2²⁴ − 1 = 16 777 215).
    pub const MAX: u32 = 0xFF_FFFF;

    /// Return the contained 24-bit big endian value
    /// as u32 in the host-native byte order.
    #[must_use]
    pub fn get(self) -> u32 {
        u32::from_be_bytes([0, self.0[0], self.0[1], self.0[2]])
    }

    /// Return the value as 24-bit big endian value
    /// or `None` if the value exceeds [`Self::MAX`].
    #[must_use]
    pub fn new(value: u32) -> Option<Self> {
        if value <= Self::MAX {
            let bytes = value.to_be_bytes();
            Some(Self([bytes[1], bytes[2], bytes[3]]))
        } else {
            None
        }
    }
}

impl From<U24> for u32 {
    fn from(v: U24) -> Self {
        v.get()
    }
}

#[cfg(test)]
mod tests {
    use zerocopy::IntoBytes;

    use super::*;
    use std::io::Write;

    #[test]
    fn u24be_roundtrip() {
        for value in [0x000000, 0x0000FF, 0x00FF00, 0x123456, 0xFF_FFFF] {
            let v = U24::new(value).unwrap();
            assert_eq!(u32::from(v), value);
        }
    }

    #[test]
    fn u24be_new_rejects_overflow() {
        assert!(U24::new(0x100_0000).is_none());
        assert!(U24::new(u32::MAX).is_none());
    }

    #[test]
    fn u24be_byte_layout() {
        assert_eq!(U24::new(0x123456).unwrap().0, [0x12, 0x34, 0x56]);
        assert_eq!(U24::new(0xFF_FFFF).unwrap().0, [0xFF, 0xFF, 0xFF]);
    }

    #[test]
    fn u24_max_value() {
        assert_eq!(U24::MAX, 16_777_215);
        assert_eq!(u32::from(U24::new(U24::MAX).unwrap()), U24::MAX);
    }

    #[test]
    fn u24_size_is_three_bytes() {
        let mut buf = Vec::new();
        buf.write_all(U24::new(1).unwrap().as_bytes()).unwrap();
        assert_eq!(buf.len(), 3);
    }
}
