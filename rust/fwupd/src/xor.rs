//! Bitwise XOR checksum computation.
//!
//! # Example
//!
//! ```
//! use fwupd::xor::xor8;
//!
//! let buf = [0x12, 0x23, 0x45, 0x67, 0x89];
//! assert_eq!(xor8(&buf), 0x9A);
//! ```


/// Error type for XOR operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The sum of `offset + n` overflowed.
    Overflow,
    /// The range `offset..offset+n` exceeds the buffer length.
    InvalidRange,
}

impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Error::Overflow => write!(f, "offset + n overflowed"),
            Error::InvalidRange => write!(f, "read out of bounds"),
        }
    }
}

/// Compute the bitwise XOR of all bytes in a buffer.
///
/// Returns 0 for an empty buffer.
///
/// # Example
///
/// ```
/// use fwupd::xor::xor8;
///
/// assert_eq!(xor8(&[0x12, 0x23, 0x45, 0x67, 0x89]), 0x9A);
/// assert_eq!(xor8(&[]), 0);
/// ```
pub fn xor8(buf: &[u8]) -> u8 {
    buf.iter().fold(0u8, |acc, b| acc ^ b)
}

/// Compute the bitwise XOR of a sub-range of a buffer, XORing
/// the result with an existing `value`.
///
/// This is the bounds-checked variant of [`xor8`]. Prefer [`xor8`] in
/// obviously correct cases or when performance matters. Use this function
/// when the offset and length come from untrusted data (e.g. device
/// firmware) and an out-of-bounds read must be handled gracefully.
///
/// Returns `Ok(value ^ xor8(buf[offset..offset+n]))`,
/// or `Err` if the range is out of bounds.
///
/// # Example
///
/// ```
/// use fwupd::xor::{xor8_safe, Error};
///
/// let buf = [0x12, 0x23, 0x45, 0x67, 0x89];
/// assert_eq!(xor8_safe(&buf, 0, 5, 0), Ok(0x9A));
///
/// // XORing again cancels out
/// assert_eq!(xor8_safe(&buf, 0, 5, 0x9A), Ok(0x00));
///
/// // Out of bounds
/// assert_eq!(xor8_safe(&buf, 0x33, 0x999, 0), Err(Error::InvalidRange));
/// ```
pub fn xor8_safe(buf: &[u8], offset: usize, n: usize, value: u8) -> Result<u8, Error> {
    let end = offset.checked_add(n).ok_or(Error::Overflow)?;
    if end > buf.len() {
        return Err(Error::InvalidRange);
    }
    Ok(value ^ xor8(&buf[offset..end]))
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_BUF: [u8; 5] = [0x12, 0x23, 0x45, 0x67, 0x89];

    #[test]
    fn xor8_basic() {
        assert_eq!(xor8(&TEST_BUF), 0x9A);
    }

    #[test]
    fn xor8_empty() {
        assert_eq!(xor8(&[]), 0);
    }

    #[test]
    fn xor8_safe_basic() {
        assert_eq!(xor8_safe(&TEST_BUF, 0, 5, 0), Ok(0x9A));
    }

    #[test]
    fn xor8_safe_accumulates() {
        let value = xor8_safe(&TEST_BUF, 0, 5, 0).unwrap();
        assert_eq!(value, 0x9A);
        // XOR again cancels out
        let value = xor8_safe(&TEST_BUF, 0, 5, value).unwrap();
        assert_eq!(value, 0x00);
    }

    #[test]
    fn xor8_safe_out_of_bounds() {
        assert_eq!(xor8_safe(&TEST_BUF, 0x33, 0x999, 0), Err(Error::InvalidRange));
    }

    #[test]
    fn xor8_safe_overflow() {
        assert_eq!(xor8_safe(&TEST_BUF, usize::MAX, 1, 0), Err(Error::Overflow));
    }
}
