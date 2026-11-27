/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use zerocopy::byteorder::big_endian::U32 as U32BE;
use zerocopy::byteorder::little_endian::U32 as U32LE;

use super::wire;

/// The checksum for a chunk of bytes, typically from a [`wire::CFData`]
/// block.
///
/// The algorithm processes 4 bytes at a time as little-endian u32 values,
/// XOR-ing each into the accumulator. The remainder bytes (1--3) use a
/// Big Endian ordering:
///
/// - 3 bytes: `b[0]<<16 | b[1]<<8 | b[2]`
/// - 2 bytes: `b[0]<<8 | b[1]`
/// - 1 byte:  `b[0]`
///
/// See [Microsoft Cabinet Archive](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85))
/// for details.
#[derive(Copy, Clone, Default, Debug)]
pub struct Checksum {
    value: u32,
}

impl Checksum {
    /// Create a new checksum with a seed of zero
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Create a new checksum based on the [`CFData`](crate::cab::wire::CFData) entry.
    ///
    /// This is a convenience function for CAB parser, the `CFData` header itself forms
    /// part of the checksum input (excluding the checksum field).
    ///
    /// ```ignore
    /// let mut csum = Checksum::new_from_cfdata(&cfdata);
    /// csum.update(payload);
    /// assert_eq!(csum.checksum(), cfdta.checksum);
    /// ```
    #[must_use]
    pub fn new_from_cfdata(cfdata: &wire::CFData) -> Self {
        let mut hdr_bytes = [0u8; 4];
        hdr_bytes[0..2].copy_from_slice(&cfdata.cbData.to_bytes());
        hdr_bytes[2..4].copy_from_slice(&cfdata.cbUncomp.to_bytes());
        let mut csum = Self::new();
        csum.update(&hdr_bytes);
        csum
    }

    #[cfg(test)]
    #[must_use]
    pub fn with_value(seed: u32) -> Checksum {
        Self { value: seed }
    }

    /// Return the calculate checksum
    #[must_use]
    pub fn checksum(&self) -> u32 {
        self.value
    }

    /// Update the checksum with all bytes in the data buffer
    /// in chunks of 4 bytes and update the internal checksum accordingly.
    ///
    /// This function is safe to be called multiple times with buffers
    /// that are a multiple of size 4. If a buffer is not of size 4
    /// it must be the last buffer provided (due to how the algorithm
    /// works).
    #[allow(clippy::missing_panics_doc)]
    pub fn update(&mut self, data: &[u8]) {
        let mut csum = self.value;
        let chunks = data.len() / 4;
        for i in 0..chunks {
            let off = i * 4;
            let bytes: [u8; 4] = data[off..off + 4].try_into().expect("cannot fail");
            csum ^= U32LE::from_bytes(bytes).get();
        }
        let remainder = &data[chunks * 4..];
        if !remainder.is_empty() {
            let mut bytes = [0u8; 4];
            bytes[4 - remainder.len()..].copy_from_slice(remainder);

            // Note: intentionally BigEndian, this is not a typo
            csum ^= U32BE::from_bytes(bytes).get();
        }
        self.value = csum;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checksum_empty() {
        let mut csum = Checksum::new();
        csum.update(&[]);
        assert_eq!(csum.checksum(), 0);
    }

    #[test]
    fn checksum_empty_with_seed() {
        let mut csum = Checksum::with_value(0xDEAD_BEEF);
        csum.update(&[]);
        assert_eq!(csum.checksum(), 0xDEADBEEF);
    }

    #[test]
    fn checksum_4_bytes() {
        let data = [0x01, 0x02, 0x03, 0x04];
        let expected = u32::from_le_bytes(data);
        let mut csum = Checksum::new();
        csum.update(&data);
        assert_eq!(csum.checksum(), expected);
    }

    #[test]
    fn checksum_8_bytes() {
        let data = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
        let a = u32::from_le_bytes([0x01, 0x02, 0x03, 0x04]);
        let b = u32::from_le_bytes([0x05, 0x06, 0x07, 0x08]);
        let mut csum = Checksum::new();
        csum.update(&data);
        assert_eq!(csum.checksum(), a ^ b);
    }

    #[test]
    fn checksum_1_remainder() {
        let data = [0x42];
        let mut csum = Checksum::new();
        csum.update(&data);
        assert_eq!(csum.checksum(), 0x42);
    }

    #[test]
    fn checksum_2_remainder() {
        let data = [0x12, 0x34];
        let expected = (0x12u32 << 8) | 0x34u32;
        let mut csum = Checksum::new();
        csum.update(&data);
        assert_eq!(csum.checksum(), expected);
    }

    #[test]
    fn checksum_3_remainder() {
        let data = [0xAA, 0xBB, 0xCC];
        let expected = (0xAAu32 << 16) | (0xBBu32 << 8) | 0xCCu32;
        let mut csum = Checksum::new();
        csum.update(&data);
        assert_eq!(csum.checksum(), expected);
    }

    #[test]
    fn checksum_5_bytes() {
        // 4 bytes processed as u32 LE, then 1 byte remainder
        let data = [0x01, 0x02, 0x03, 0x04, 0x05];
        let word = u32::from_le_bytes([0x01, 0x02, 0x03, 0x04]);
        let remainder = 0x05u32;
        let mut csum = Checksum::new();
        csum.update(&data);
        assert_eq!(csum.checksum(), word ^ remainder);
    }

    #[test]
    fn checksum_6_bytes() {
        let data = [0x01, 0x02, 0x03, 0x04, 0x10, 0x20];
        let word = u32::from_le_bytes([0x01, 0x02, 0x03, 0x04]);
        let remainder = (0x10u32 << 8) | 0x20u32;
        let mut csum = Checksum::new();
        csum.update(&data);
        assert_eq!(csum.checksum(), word ^ remainder);
    }

    #[test]
    fn checksum_7_bytes() {
        let data = [0x01, 0x02, 0x03, 0x04, 0xAA, 0xBB, 0xCC];
        let word = u32::from_le_bytes([0x01, 0x02, 0x03, 0x04]);
        let remainder = (0xAAu32 << 16) | (0xBBu32 << 8) | 0xCCu32;
        let mut csum = Checksum::new();
        csum.update(&data);
        assert_eq!(csum.checksum(), word ^ remainder);
    }

    #[test]
    fn checksum_with_seed() {
        let data = [0x01, 0x02, 0x03, 0x04];
        let word = u32::from_le_bytes(data);
        let seed = 0x12345678u32;
        let mut csum = Checksum::with_value(seed);
        csum.update(&data);
        assert_eq!(csum.checksum(), seed ^ word);
    }

    #[test]
    fn checksum_idempotent() {
        let data = [1, 2, 3, 4, 5, 6, 7, 8, 9];
        let mut c1 = Checksum::new();
        c1.update(&data);
        let mut c2 = Checksum::new();
        c2.update(&data);
        assert_eq!(c1.checksum(), c2.checksum());
    }

    #[test]
    fn checksum_two_pass() {
        // Two update() calls accumulate correctly when the first chunk is 4-byte aligned.
        let payload = [0x01, 0x02, 0x03, 0x04];
        let header = [0x05, 0x06, 0x07, 0x08];
        let mut csum = Checksum::new();
        csum.update(&payload);
        csum.update(&header);
        let p = u32::from_le_bytes(payload);
        let h = u32::from_le_bytes(header);
        assert_eq!(csum.checksum(), p ^ h);
    }
}
