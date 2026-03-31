//! Arithmetic sum checksum computation for 8-, 16-, and 32-bit widths.
//!
//! # Example
//!
//! ```
//! use fwupd::sum::{sum8, sum16, sum32};
//!
//! let buf = [0x01, 0x02, 0x03, 0x04];
//! assert_eq!(sum8(&buf), 10);
//! assert_eq!(sum16(&buf), 10);
//! assert_eq!(sum32(&buf), 10);
//! ```


/// Error type for sum operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The sum of `offset + n` overflowed.
    Overflow,
    /// The range `offset..offset+n` exceeds the buffer length.
    InvalidRange,
    /// The buffer length is not a multiple of the word size.
    InvalidAlignment,
}

impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Error::Overflow => write!(f, "offset + n overflowed"),
            Error::InvalidRange => write!(f, "read out of bounds"),
            Error::InvalidAlignment => write!(f, "buffer length not a multiple of word size"),
        }
    }
}

/// Endian type matching the C `FuEndianType` / GLib constants.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Endian {
    Big,
    Little,
}

/// Compute the arithmetic sum of all bytes in a buffer (8-bit wrapping).
pub fn sum8(buf: &[u8]) -> u8 {
    buf.iter().fold(0u8, |acc, b| acc.wrapping_add(*b))
}

/// Compute the arithmetic sum of a sub-range of a buffer (8-bit).
///
/// This is the bounds-checked variant of [`sum8`]. Prefer [`sum8`] in
/// obviously correct cases or when performance matters. Use this function
/// when the offset and length come from untrusted data (e.g. device
/// firmware) and an out-of-bounds read must be handled gracefully.
///
/// Returns `Err` if the range `offset..offset+n` is out of bounds.
pub fn sum8_safe(buf: &[u8], offset: usize, n: usize) -> Result<u8, Error> {
    let end = offset.checked_add(n).ok_or(Error::Overflow)?;
    if end > buf.len() {
        return Err(Error::InvalidRange);
    }
    Ok(sum8(&buf[offset..end]))
}

/// Compute the arithmetic sum of all bytes in a buffer, adding them one
/// byte at a time into a 16-bit accumulator.
pub fn sum16(buf: &[u8]) -> u16 {
    buf.iter().fold(0u16, |acc, b| acc.wrapping_add(*b as u16))
}

/// Compute the arithmetic sum of a sub-range of a buffer (16-bit).
///
/// This is the bounds-checked variant of [`sum16`]. Prefer [`sum16`] in
/// obviously correct cases or when performance matters. Use this function
/// when the offset and length come from untrusted data (e.g. device
/// firmware) and an out-of-bounds read must be handled gracefully.
///
/// Returns `Err` if the range `offset..offset+n` is out of bounds.
pub fn sum16_safe(buf: &[u8], offset: usize, n: usize) -> Result<u16, Error> {
    let end = offset.checked_add(n).ok_or(Error::Overflow)?;
    if end > buf.len() {
        return Err(Error::InvalidRange);
    }
    Ok(sum16(&buf[offset..end]))
}

/// Compute the arithmetic sum of 16-bit words in a buffer, adding them
/// one word at a time.
///
/// The buffer is interpreted as a sequence of 16-bit words with the given
/// endianness. The buffer length must be a multiple of 2; returns
/// `Err(Error::InvalidAlignment)` otherwise.
pub fn sum16w(buf: &[u8], endian: Endian) -> Result<u16, Error> {
    if buf.len() % 2 != 0 {
        return Err(Error::InvalidAlignment);
    }
    let result = buf.chunks_exact(2).fold(0u16, |acc, chunk| {
        let word = match endian {
            Endian::Little => u16::from_le_bytes([chunk[0], chunk[1]]),
            Endian::Big => u16::from_be_bytes([chunk[0], chunk[1]]),
        };
        acc.wrapping_add(word)
    });
    Ok(result)
}

/// Compute the arithmetic sum of all bytes in a buffer, adding them one
/// byte at a time into a 32-bit accumulator.
pub fn sum32(buf: &[u8]) -> u32 {
    buf.iter().fold(0u32, |acc, b| acc.wrapping_add(*b as u32))
}

/// Compute the arithmetic sum of 32-bit dwords in a buffer, adding them
/// one dword at a time.
///
/// The buffer is interpreted as a sequence of 32-bit dwords with the given
/// endianness. The buffer length must be a multiple of 4; returns
/// `Err(Error::InvalidAlignment)` otherwise.
pub fn sum32w(buf: &[u8], endian: Endian) -> Result<u32, Error> {
    if buf.len() % 4 != 0 {
        return Err(Error::InvalidAlignment);
    }
    let result = buf.chunks_exact(4).fold(0u32, |acc, chunk| {
        let dword = match endian {
            Endian::Little => u32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]),
            Endian::Big => u32::from_be_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]),
        };
        acc.wrapping_add(dword)
    });
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_BUF: [u8; 5] = [0x12, 0x23, 0x45, 0x67, 0x89];

    #[test]
    fn test_sum8() {
        // 0x12 + 0x23 + 0x45 + 0x67 + 0x89 = 0x16A, truncated to 0x6A
        assert_eq!(sum8(&TEST_BUF), 0x6A);
        assert_eq!(sum8(&[]), 0);
    }

    #[test]
    fn test_sum8_safe() {
        assert_eq!(sum8_safe(&TEST_BUF, 0, 5), Ok(0x6A));
        assert_eq!(sum8_safe(&TEST_BUF, 0x33, 0x999), Err(Error::InvalidRange));
        assert_eq!(sum8_safe(&TEST_BUF, usize::MAX, 1), Err(Error::Overflow));
    }

    #[test]
    fn test_sum16() {
        // 0x12 + 0x23 + 0x45 + 0x67 + 0x89 = 0x016A
        assert_eq!(sum16(&TEST_BUF), 0x016A);
    }

    #[test]
    fn test_sum16_safe() {
        assert_eq!(sum16_safe(&TEST_BUF, 0, 5), Ok(0x016A));
        assert_eq!(sum16_safe(&TEST_BUF, 0x33, 0x999), Err(Error::InvalidRange));
    }

    #[test]
    fn test_sum16w() {
        let buf = [0x01, 0x00, 0x02, 0x00]; // LE: 0x0001 + 0x0002 = 3
        assert_eq!(sum16w(&buf, Endian::Little), Ok(3));
        // BE: 0x0100 + 0x0200 = 0x0300
        assert_eq!(sum16w(&buf, Endian::Big), Ok(0x0300));
        // Odd length
        assert_eq!(sum16w(&[0x01, 0x02, 0x03], Endian::Little), Err(Error::InvalidAlignment));
    }

    #[test]
    fn test_sum32() {
        assert_eq!(sum32(&TEST_BUF), 0x0000016A);
    }

    #[test]
    fn test_sum32w() {
        let buf = [0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00]; // LE: 1 + 2 = 3
        assert_eq!(sum32w(&buf, Endian::Little), Ok(3));
        // Not a multiple of 4
        assert_eq!(sum32w(&[0x01, 0x02, 0x03], Endian::Little), Err(Error::InvalidAlignment));
    }
}
