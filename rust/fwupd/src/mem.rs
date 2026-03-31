/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Memory utilities with bounds checking and endian support.
//!
//! This module provides:
//! - Bounds-checked range validation (`check_read_range`, `check_write_range`)
//! - Endian-aware integer read/write via the [`MemReadWrite`] trait (`memread`, `memwrite`)
//! - Bounded memory copy (`memcpy`) and comparison (`memcmp`)
//! - Needle-in-haystack search (`memmem`)
//! - Memory duplication (`memdup`)
//! - NUL-terminated string extraction (`string_from_offset`)
//! - ASCII string extraction (`memstr`)

/// Endian type for integer read/write operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Endian {
    Big,
    Little,
}

// ---------------------------------------------------------------------------
// Bounds checking
// ---------------------------------------------------------------------------

/// The kind of memory error.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MemErrorKind {
    Read,
    Write,
    NotFound,
    InvalidData,
    NotSupported,
}

/// Error type for memory operations.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MemError {
    pub kind: MemErrorKind,
    pub message: String,
}

impl core::fmt::Display for MemError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(&self.message)
    }
}

/// Check whether reading `n` bytes at `offset` from a buffer of size `bufsz` is safe.
///
/// Prefer direct slice indexing in obviously correct cases or when performance
/// matters. Use this function when the offset and length come from untrusted
/// data (e.g. device firmware) and an out-of-bounds read must be detected
/// without risking a panic.
///
/// Returns `Ok(())` if the read is within bounds, or `Err` otherwise.
pub fn check_read_range(bufsz: usize, offset: usize, n: usize) -> Result<(), MemError> {
    if n == 0 {
        return Ok(());
    }
    if n > bufsz {
        return Err(MemError {
            kind: MemErrorKind::Read,
            message: format!(
                "attempted to read 0x{:02x} bytes from buffer of 0x{:02x}",
                n, bufsz
            ),
        });
    }
    if offset.checked_add(n).is_none() {
        return Err(MemError {
            kind: MemErrorKind::Read,
            message: format!("offset 0x{:02x} + 0x{:02x} overflowed", offset, n),
        });
    }
    if offset > bufsz || n + offset > bufsz {
        return Err(MemError {
            kind: MemErrorKind::Read,
            message: format!(
                "attempted to read 0x{:02x} bytes at offset 0x{:02x} from buffer of 0x{:02x}",
                n, offset, bufsz
            ),
        });
    }
    Ok(())
}

/// Check whether writing `n` bytes at `offset` to a buffer of size `bufsz` is safe.
///
/// Prefer direct slice indexing in obviously correct cases or when performance
/// matters. Use this function when the offset and length come from untrusted
/// data (e.g. device firmware) and an out-of-bounds write must be detected
/// without risking a panic.
///
/// Returns `Ok(())` if the write is within bounds, or `Err` otherwise.
pub fn check_write_range(bufsz: usize, offset: usize, n: usize) -> Result<(), MemError> {
    if n == 0 {
        return Ok(());
    }
    if n > bufsz {
        return Err(MemError {
            kind: MemErrorKind::Write,
            message: format!(
                "attempted to write 0x{:02x} bytes to buffer of 0x{:02x}",
                n, bufsz
            ),
        });
    }
    if offset.checked_add(n).is_none() {
        return Err(MemError {
            kind: MemErrorKind::Write,
            message: format!("offset 0x{:02x} + 0x{:02x} overflowed", offset, n),
        });
    }
    if offset > bufsz || n + offset > bufsz {
        return Err(MemError {
            kind: MemErrorKind::Write,
            message: format!(
                "attempted to write 0x{:02x} bytes at offset 0x{:02x} to buffer of 0x{:02x}",
                n, offset, bufsz
            ),
        });
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// NUL-terminated string read
// ---------------------------------------------------------------------------

/// Read a NUL-terminated string from `buf` starting at `offset`.
///
/// Scans forward from `offset` looking for a NUL byte. Returns the bytes
/// between `offset` and the NUL as a `String`. Returns `Err` if `offset`
/// is out of bounds or no NUL terminator is found.
pub fn string_from_offset(buf: &[u8], offset: usize) -> Result<String, MemError> {
    if offset >= buf.len() {
        return Err(MemError {
            kind: MemErrorKind::InvalidData,
            message: format!(
                "string offset 0x{:x} exceeds buffer size 0x{:x}",
                offset,
                buf.len()
            ),
        });
    }
    for i in offset..buf.len() {
        if buf[i] == 0 {
            // Safe: we only collected bytes up to the NUL, callers expect
            // UTF-8 but the C version treats it as opaque bytes in a GString.
            // Use from_utf8_lossy to be robust.
            let s = String::from_utf8_lossy(&buf[offset..i]).into_owned();
            return Ok(s);
        }
    }
    Err(MemError {
        kind: MemErrorKind::InvalidData,
        message: "buffer not NULL terminated".to_string(),
    })
}

// ---------------------------------------------------------------------------
// Safe memcpy
// ---------------------------------------------------------------------------

/// Bounds-checked memory copy from `src[src_offset..]` to `dst[dst_offset..]`.
///
/// Copies `n` bytes. Returns `Err` if either the source read or destination
/// write would be out of bounds.
///
/// Pass the full source and destination slices and use the offset parameters
/// to specify where to read/write. Do not pre-slice the buffers, as that
/// defeats the bounds checking.
pub fn memcpy(
    dst: &mut [u8],
    dst_offset: usize,
    src: &[u8],
    src_offset: usize,
    n: usize,
) -> Result<(), MemError> {
    check_read_range(src.len(), src_offset, n)?;
    check_write_range(dst.len(), dst_offset, n)?;
    dst[dst_offset..dst_offset + n].copy_from_slice(&src[src_offset..src_offset + n]);
    Ok(())
}

// ---------------------------------------------------------------------------
// Safe memcmp
// ---------------------------------------------------------------------------

/// Bounds-checked comparison of `n` bytes from `buf1[buf1_offset..]` and
/// `buf2[buf2_offset..]`.
///
/// Returns `Ok(())` if the regions are identical, or `Err` describing the
/// first mismatch (or out-of-bounds access).
pub fn memcmp(
    buf1: &[u8],
    buf1_offset: usize,
    buf2: &[u8],
    buf2_offset: usize,
    n: usize,
) -> Result<(), MemError> {
    check_read_range(buf1.len(), buf1_offset, n)?;
    check_read_range(buf2.len(), buf2_offset, n)?;
    for i in 0..n {
        let a = buf1[buf1_offset + i];
        let b = buf2[buf2_offset + i];
        if a != b {
            return Err(MemError {
                kind: MemErrorKind::InvalidData,
                message: format!("got 0x{:02x}, expected 0x{:02x} @ 0x{:04x}", a, b, i),
            });
        }
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// Safe memmem (needle in haystack)
// ---------------------------------------------------------------------------

/// Find the first occurrence of `needle` in `haystack`.
///
/// Returns `Ok(offset)` with the byte offset of the match, or `Err` if not
/// found or the needle is larger than the haystack.
pub fn memmem(haystack: &[u8], needle: &[u8]) -> Result<usize, MemError> {
    if needle.is_empty() {
        return Ok(0);
    }
    if needle.len() > haystack.len() {
        return Err(MemError {
            kind: MemErrorKind::NotFound,
            message: format!(
                "needle of 0x{:02x} bytes is larger than haystack of 0x{:02x} bytes",
                needle.len(),
                haystack.len()
            ),
        });
    }
    for i in 0..=haystack.len() - needle.len() {
        if haystack[i..i + needle.len()] == *needle {
            return Ok(i);
        }
    }
    Err(MemError {
        kind: MemErrorKind::NotFound,
        message: format!(
            "needle of 0x{:02x} bytes was not found in haystack of 0x{:02x} bytes",
            needle.len(),
            haystack.len()
        ),
    })
}

// ---------------------------------------------------------------------------
// Safe memdup
// ---------------------------------------------------------------------------

/// Maximum allocation size (1 GB), matching the C implementation.
const MAX_ALLOC_SIZE: usize = 0x40000000;

/// Duplicate `n` bytes from `src` into a new `Vec<u8>`.
///
/// Returns `Err` if `n` exceeds the 1 GB safety limit.
pub fn memdup(src: &[u8], n: usize) -> Result<Vec<u8>, MemError> {
    if n > MAX_ALLOC_SIZE {
        return Err(MemError {
            kind: MemErrorKind::NotSupported,
            message: format!("cannot allocate {}GB of memory", n / MAX_ALLOC_SIZE),
        });
    }
    if n > src.len() {
        return Err(MemError {
            kind: MemErrorKind::Read,
            message: format!(
                "attempted to duplicate 0x{:02x} bytes from buffer of 0x{:02x}",
                n,
                src.len()
            ),
        });
    }
    Ok(src[..n].to_vec())
}

// ---------------------------------------------------------------------------
// Safe memstr (extract ASCII string)
// ---------------------------------------------------------------------------

/// Extract an ASCII string from `buf[offset..offset+maxsz]`.
///
/// Non-printable characters are stripped. Returns `Err` if the region is out
/// of bounds or contains no valid ASCII characters.
pub fn memstr(buf: &[u8], offset: usize, maxsz: usize) -> Result<String, MemError> {
    check_read_range(buf.len(), offset, maxsz)?;
    let region = &buf[offset..offset + maxsz];
    // Replicate fu_strsafe: keep only printable ASCII, trim, return None if empty
    let s: String = region
        .iter()
        .take_while(|&&b| b != 0) // stop at NUL
        .filter(|&&b| (0x20..=0x7e).contains(&b)) // printable ASCII
        .map(|&b| b as char)
        .collect();
    let s = s.trim().to_string();
    if s.is_empty() {
        return Err(MemError {
            kind: MemErrorKind::InvalidData,
            message: "invalid ASCII string".to_string(),
        });
    }
    Ok(s)
}

// ---------------------------------------------------------------------------
// Endian-aware memread/memwrite — generic trait + implementations
// ---------------------------------------------------------------------------

/// Trait for types that can be read from / written to byte buffers with
/// endian awareness. Implemented for `u16`, `u32`, and `u64`.
///
/// 24-bit values are handled separately since there is no native 24-bit
/// integer type. `u8` ignores the endian parameter.
pub trait MemReadWrite: Copy + Sized {
    /// Number of bytes this type occupies.
    const SIZE: usize;

    /// Read a value from `buf` (must be at least `SIZE` bytes) with the
    /// given endianness.
    fn mem_read(buf: &[u8], endian: Endian) -> Self;

    /// Write a value to `buf` (must be at least `SIZE` bytes) with the
    /// given endianness.
    fn mem_write(buf: &mut [u8], val: Self, endian: Endian);
}

impl MemReadWrite for u8 {
    const SIZE: usize = 1;

    fn mem_read(buf: &[u8], _endian: Endian) -> Self {
        buf[0]
    }

    fn mem_write(buf: &mut [u8], val: Self, _endian: Endian) {
        buf[0] = val;
    }
}

impl MemReadWrite for u16 {
    const SIZE: usize = 2;

    fn mem_read(buf: &[u8], endian: Endian) -> Self {
        let bytes: [u8; 2] = [buf[0], buf[1]];
        match endian {
            Endian::Big => u16::from_be_bytes(bytes),
            Endian::Little => u16::from_le_bytes(bytes),
        }
    }

    fn mem_write(buf: &mut [u8], val: Self, endian: Endian) {
        let bytes = match endian {
            Endian::Big => val.to_be_bytes(),
            Endian::Little => val.to_le_bytes(),
        };
        buf[..2].copy_from_slice(&bytes);
    }
}

impl MemReadWrite for u32 {
    const SIZE: usize = 4;

    fn mem_read(buf: &[u8], endian: Endian) -> Self {
        let bytes: [u8; 4] = [buf[0], buf[1], buf[2], buf[3]];
        match endian {
            Endian::Big => u32::from_be_bytes(bytes),
            Endian::Little => u32::from_le_bytes(bytes),
        }
    }

    fn mem_write(buf: &mut [u8], val: Self, endian: Endian) {
        let bytes = match endian {
            Endian::Big => val.to_be_bytes(),
            Endian::Little => val.to_le_bytes(),
        };
        buf[..4].copy_from_slice(&bytes);
    }
}

impl MemReadWrite for u64 {
    const SIZE: usize = 8;

    fn mem_read(buf: &[u8], endian: Endian) -> Self {
        let bytes: [u8; 8] = [
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
        ];
        match endian {
            Endian::Big => u64::from_be_bytes(bytes),
            Endian::Little => u64::from_le_bytes(bytes),
        }
    }

    fn mem_write(buf: &mut [u8], val: Self, endian: Endian) {
        let bytes = match endian {
            Endian::Big => val.to_be_bytes(),
            Endian::Little => val.to_le_bytes(),
        };
        buf[..8].copy_from_slice(&bytes);
    }
}

/// Read a value of type `T` from `buf` at `offset` with the given endianness.
///
/// Returns `Err` if the read would be out of bounds.
pub fn memread<T: MemReadWrite>(buf: &[u8], offset: usize, endian: Endian) -> Result<T, MemError> {
    check_read_range(buf.len(), offset, T::SIZE)?;
    Ok(T::mem_read(&buf[offset..], endian))
}

/// Write a value of type `T` to `buf` at `offset` with the given endianness.
///
/// Returns `Err` if the write would be out of bounds.
pub fn memwrite<T: MemReadWrite>(
    buf: &mut [u8],
    offset: usize,
    value: T,
    endian: Endian,
) -> Result<(), MemError> {
    check_write_range(buf.len(), offset, T::SIZE)?;
    T::mem_write(&mut buf[offset..], value, endian);
    Ok(())
}

// ---------------------------------------------------------------------------
// Public: U24
// ---------------------------------------------------------------------------

/// A 24-bit unsigned integer, stored as a `u32` with the upper 8 bits zeroed.
///
/// This type exists so that 24-bit values can use the generic [`memread`]
/// and [`memwrite`] functions via the [`MemReadWrite`] trait.
///
/// # Example
///
/// ```
/// use fwupd::mem::{U24, memread, memwrite, Endian};
///
/// let mut buf = [0u8; 3];
/// memwrite(&mut buf, 0, U24::new(0xABCDEF), Endian::Big).unwrap();
/// assert_eq!(buf, [0xAB, 0xCD, 0xEF]);
/// assert_eq!(memread::<U24>(&buf, 0, Endian::Big).unwrap().value(), 0xABCDEF);
/// ```
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct U24(u32);

impl U24 {
    /// Create a `U24` from a `u32`, masking to the lower 24 bits.
    pub fn new(val: u32) -> Self {
        Self(val & 0x00FFFFFF)
    }

    /// Return the value as a `u32`.
    pub fn value(self) -> u32 {
        self.0
    }
}

impl MemReadWrite for U24 {
    const SIZE: usize = 3;

    fn mem_read(buf: &[u8], endian: Endian) -> Self {
        let val = match endian {
            Endian::Big => (buf[0] as u32) << 16 | (buf[1] as u32) << 8 | buf[2] as u32,
            Endian::Little => buf[0] as u32 | (buf[1] as u32) << 8 | (buf[2] as u32) << 16,
        };
        U24(val)
    }

    fn mem_write(buf: &mut [u8], val: Self, endian: Endian) {
        match endian {
            Endian::Big => {
                buf[0] = (val.0 >> 16) as u8;
                buf[1] = (val.0 >> 8) as u8;
                buf[2] = val.0 as u8;
            }
            Endian::Little => {
                buf[0] = val.0 as u8;
                buf[1] = (val.0 >> 8) as u8;
                buf[2] = (val.0 >> 16) as u8;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    // -- string_from_offset --

    #[test]
    fn string_from_offset_basic() {
        let buf = b"hello\x00world\x00";
        let s = string_from_offset(buf, 0).unwrap();
        assert_eq!(s, "hello");
    }

    #[test]
    fn string_from_offset_with_offset() {
        let buf = b"hello\x00world\x00";
        let s = string_from_offset(buf, 6).unwrap();
        assert_eq!(s, "world");
    }

    #[test]
    fn string_from_offset_empty_string() {
        let buf = b"\x00rest";
        let s = string_from_offset(buf, 0).unwrap();
        assert_eq!(s, "");
    }

    #[test]
    fn string_from_offset_offset_at_end() {
        let buf = b"hello\x00";
        assert!(string_from_offset(buf, 6).is_err());
    }

    #[test]
    fn string_from_offset_offset_past_end() {
        let buf = b"hello\x00";
        assert!(string_from_offset(buf, 100).is_err());
    }

    #[test]
    fn string_from_offset_no_nul() {
        let buf = b"hello";
        assert!(string_from_offset(buf, 0).is_err());
    }

    #[test]
    fn string_from_offset_empty_buf() {
        let buf: &[u8] = &[];
        assert!(string_from_offset(buf, 0).is_err());
    }

    // -- check_read_range --

    #[test]
    fn check_read_range_zero_n() {
        assert!(check_read_range(0, 0, 0).is_ok());
        assert!(check_read_range(10, 5, 0).is_ok());
    }

    #[test]
    fn check_read_range_ok() {
        assert!(check_read_range(10, 0, 10).is_ok());
        assert!(check_read_range(10, 5, 5).is_ok());
    }

    #[test]
    fn check_read_range_n_exceeds_bufsz() {
        assert!(check_read_range(5, 0, 10).is_err());
    }

    #[test]
    fn check_read_range_offset_overflow() {
        assert!(check_read_range(10, usize::MAX, 1).is_err());
    }

    #[test]
    fn check_read_range_past_end() {
        assert!(check_read_range(10, 8, 5).is_err());
    }

    // -- check_write_range --

    #[test]
    fn check_write_range_zero_n() {
        assert!(check_write_range(0, 0, 0).is_ok());
    }

    #[test]
    fn check_write_range_ok() {
        assert!(check_write_range(10, 0, 10).is_ok());
    }

    #[test]
    fn check_write_range_n_exceeds_bufsz() {
        assert!(check_write_range(5, 0, 10).is_err());
    }

    #[test]
    fn check_write_range_offset_overflow() {
        assert!(check_write_range(10, usize::MAX, 1).is_err());
    }

    #[test]
    fn check_write_range_past_end() {
        assert!(check_write_range(10, 8, 5).is_err());
    }

    // -- memcpy --

    #[test]
    fn memcpy_basic() {
        let src = [1, 2, 3, 4, 5];
        let mut dst = [0u8; 5];
        assert!(memcpy(&mut dst, 0, &src, 0, 5).is_ok());
        assert_eq!(dst, [1, 2, 3, 4, 5]);
    }

    #[test]
    fn memcpy_with_offsets() {
        let src = [10, 20, 30, 40, 50];
        let mut dst = [0u8; 10];
        assert!(memcpy(&mut dst, 3, &src, 1, 3).is_ok());
        assert_eq!(dst, [0, 0, 0, 20, 30, 40, 0, 0, 0, 0]);
    }

    #[test]
    fn memcpy_src_oob() {
        let src = [1, 2, 3];
        let mut dst = [0u8; 10];
        assert!(memcpy(&mut dst, 0, &src, 2, 5).is_err());
    }

    #[test]
    fn memcpy_dst_oob() {
        let src = [1, 2, 3, 4, 5];
        let mut dst = [0u8; 3];
        assert!(memcpy(&mut dst, 0, &src, 0, 5).is_err());
    }

    #[test]
    fn memcpy_zero_bytes() {
        let src = [1, 2, 3];
        let mut dst = [0u8; 3];
        assert!(memcpy(&mut dst, 0, &src, 0, 0).is_ok());
        assert_eq!(dst, [0, 0, 0]);
    }

    // -- memcmp --

    #[test]
    fn memcmp_equal() {
        let a = [1, 2, 3, 4, 5];
        let b = [1, 2, 3, 4, 5];
        assert!(memcmp(&a, 0, &b, 0, 5).is_ok());
    }

    #[test]
    fn memcmp_partial_equal() {
        let a = [0, 0, 1, 2, 3];
        let b = [1, 2, 3, 0, 0];
        assert!(memcmp(&a, 2, &b, 0, 3).is_ok());
    }

    #[test]
    fn memcmp_mismatch() {
        let a = [1, 2, 3];
        let b = [1, 2, 4];
        let err = memcmp(&a, 0, &b, 0, 3).unwrap_err();
        assert!(err.message.contains("0x03"));
        assert!(err.message.contains("0x04"));
    }

    #[test]
    fn memcmp_oob() {
        let a = [1, 2, 3];
        let b = [1, 2, 3, 4, 5];
        assert!(memcmp(&a, 0, &b, 0, 5).is_err());
    }

    // -- memmem --

    #[test]
    fn memmem_found() {
        let haystack = [0, 1, 2, 3, 4, 5, 6];
        let needle = [3, 4, 5];
        assert_eq!(memmem(&haystack, &needle), Ok(3));
    }

    #[test]
    fn memmem_at_start() {
        let haystack = [1, 2, 3, 4, 5];
        let needle = [1, 2];
        assert_eq!(memmem(&haystack, &needle), Ok(0));
    }

    #[test]
    fn memmem_at_end() {
        let haystack = [1, 2, 3, 4, 5];
        let needle = [4, 5];
        assert_eq!(memmem(&haystack, &needle), Ok(3));
    }

    #[test]
    fn memmem_not_found() {
        let haystack = [1, 2, 3, 4, 5];
        let needle = [6, 7];
        assert!(memmem(&haystack, &needle).is_err());
    }

    #[test]
    fn memmem_needle_too_large() {
        let haystack = [1, 2];
        let needle = [1, 2, 3, 4, 5];
        assert!(memmem(&haystack, &needle).is_err());
    }

    #[test]
    fn memmem_empty_needle() {
        let haystack = [1, 2, 3];
        assert_eq!(memmem(&haystack, &[]), Ok(0));
    }

    // -- memdup --

    #[test]
    fn memdup_basic() {
        let src = [1, 2, 3, 4, 5];
        let result = memdup(&src, 5).unwrap();
        assert_eq!(result, vec![1, 2, 3, 4, 5]);
    }

    #[test]
    fn memdup_partial() {
        let src = [10, 20, 30, 40, 50];
        let result = memdup(&src, 3).unwrap();
        assert_eq!(result, vec![10, 20, 30]);
    }

    #[test]
    fn memdup_too_large() {
        let src = [0u8; 4];
        assert!(memdup(&src, 0x40000001).is_err());
    }

    // -- memstr --

    #[test]
    fn memstr_basic() {
        let buf = b"Hello\x00World";
        let result = memstr(buf, 0, 11).unwrap();
        assert_eq!(result, "Hello");
    }

    #[test]
    fn memstr_with_offset() {
        let buf = b"\x00\x00Hi there\x00";
        let result = memstr(buf, 2, 8).unwrap();
        assert_eq!(result, "Hi there");
    }

    #[test]
    fn memstr_non_printable_stripped() {
        let buf = [0x01, 0x02, b'A', b'B', 0x7f, b'C', 0x00];
        let result = memstr(&buf, 0, 7).unwrap();
        assert_eq!(result, "ABC");
    }

    #[test]
    fn memstr_empty() {
        let buf = [0x00, 0x01, 0x02];
        assert!(memstr(&buf, 0, 3).is_err());
    }

    #[test]
    fn memstr_oob() {
        let buf = [b'A', b'B'];
        assert!(memstr(&buf, 1, 5).is_err());
    }

    // -- memread/memwrite uint16 --

    #[test]
    fn memread_uint16_le() {
        let buf = [0x34, 0x12];
        assert_eq!(memread::<u16>(&buf, 0, Endian::Little).unwrap(), 0x1234);
    }

    #[test]
    fn memread_uint16_be() {
        let buf = [0x12, 0x34];
        assert_eq!(memread::<u16>(&buf, 0, Endian::Big).unwrap(), 0x1234);
    }

    #[test]
    fn memwrite_uint16_le() {
        let mut buf = [0u8; 2];
        memwrite(&mut buf, 0, 0x1234u16, Endian::Little).unwrap();
        assert_eq!(buf, [0x34, 0x12]);
    }

    #[test]
    fn memwrite_uint16_be() {
        let mut buf = [0u8; 2];
        memwrite(&mut buf, 0, 0x1234u16, Endian::Big).unwrap();
        assert_eq!(buf, [0x12, 0x34]);
    }

    // -- memread/memwrite uint24 --

    #[test]
    fn memread_uint24_le() {
        let buf = [0x56, 0x34, 0x12];
        assert_eq!(
            memread::<U24>(&buf, 0, Endian::Little).unwrap().value(),
            0x123456
        );
    }

    #[test]
    fn memread_uint24_be() {
        let buf = [0x12, 0x34, 0x56];
        assert_eq!(
            memread::<U24>(&buf, 0, Endian::Big).unwrap().value(),
            0x123456
        );
    }

    #[test]
    fn memwrite_uint24_le() {
        let mut buf = [0u8; 3];
        memwrite(&mut buf, 0, U24::new(0x123456), Endian::Little).unwrap();
        assert_eq!(buf, [0x56, 0x34, 0x12]);
    }

    #[test]
    fn memwrite_uint24_be() {
        let mut buf = [0u8; 3];
        memwrite(&mut buf, 0, U24::new(0x123456), Endian::Big).unwrap();
        assert_eq!(buf, [0x12, 0x34, 0x56]);
    }

    // -- memread/memwrite uint32 --

    #[test]
    fn memread_uint32_le() {
        let buf = [0x78, 0x56, 0x34, 0x12];
        assert_eq!(memread::<u32>(&buf, 0, Endian::Little).unwrap(), 0x12345678);
    }

    #[test]
    fn memread_uint32_be() {
        let buf = [0x12, 0x34, 0x56, 0x78];
        assert_eq!(memread::<u32>(&buf, 0, Endian::Big).unwrap(), 0x12345678);
    }

    #[test]
    fn memwrite_uint32_le() {
        let mut buf = [0u8; 4];
        memwrite(&mut buf, 0, 0x12345678u32, Endian::Little).unwrap();
        assert_eq!(buf, [0x78, 0x56, 0x34, 0x12]);
    }

    #[test]
    fn memwrite_uint32_be() {
        let mut buf = [0u8; 4];
        memwrite(&mut buf, 0, 0x12345678u32, Endian::Big).unwrap();
        assert_eq!(buf, [0x12, 0x34, 0x56, 0x78]);
    }

    // -- memread/memwrite uint64 --

    #[test]
    fn memread_uint64_le() {
        let buf = [0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12];
        assert_eq!(
            memread::<u64>(&buf, 0, Endian::Little).unwrap(),
            0x1234567890ABCDEF
        );
    }

    #[test]
    fn memread_uint64_be() {
        let buf = [0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF];
        assert_eq!(
            memread::<u64>(&buf, 0, Endian::Big).unwrap(),
            0x1234567890ABCDEF
        );
    }

    #[test]
    fn memwrite_uint64_le() {
        let mut buf = [0u8; 8];
        memwrite(&mut buf, 0, 0x1234567890ABCDEFu64, Endian::Little).unwrap();
        assert_eq!(buf, [0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12]);
    }

    #[test]
    fn memwrite_uint64_be() {
        let mut buf = [0u8; 8];
        memwrite(&mut buf, 0, 0x1234567890ABCDEFu64, Endian::Big).unwrap();
        assert_eq!(buf, [0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF]);
    }

    // -- safe memread --

    #[test]
    fn memread_uint8_range_ok() {
        let buf = [0x42, 0x43, 0x44];
        assert_eq!(memread::<u8>(&buf, 1, Endian::Little), Ok(0x43));
    }

    #[test]
    fn memread_uint8_range_oob() {
        let buf = [0x42];
        assert!(memread::<u8>(&buf, 1, Endian::Little).is_err());
    }

    #[test]
    fn memread_uint16_range_ok() {
        let buf = [0x00, 0x34, 0x12, 0x00];
        assert_eq!(memread::<u16>(&buf, 1, Endian::Little), Ok(0x1234));
    }

    #[test]
    fn memread_uint16_range_oob() {
        let buf = [0x34];
        assert!(memread::<u16>(&buf, 0, Endian::Little).is_err());
    }

    #[test]
    fn memread_uint24_range_ok() {
        let buf = [0x00, 0x56, 0x34, 0x12, 0x00];
        assert_eq!(
            memread::<U24>(&buf, 1, Endian::Little).map(|v| v.value()),
            Ok(0x123456)
        );
    }

    #[test]
    fn memread_uint24_range_oob() {
        let buf = [0x56, 0x34];
        assert!(memread::<U24>(&buf, 0, Endian::Little).is_err());
    }

    #[test]
    fn memread_uint32_range_ok() {
        let buf = [0x78, 0x56, 0x34, 0x12];
        assert_eq!(memread::<u32>(&buf, 0, Endian::Little), Ok(0x12345678));
    }

    #[test]
    fn memread_uint32_range_oob() {
        let buf = [0x78, 0x56, 0x34];
        assert!(memread::<u32>(&buf, 0, Endian::Little).is_err());
    }

    #[test]
    fn memread_uint64_range_ok() {
        let buf = [0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12];
        assert_eq!(
            memread::<u64>(&buf, 0, Endian::Little),
            Ok(0x1234567890ABCDEF)
        );
    }

    #[test]
    fn memread_uint64_range_oob() {
        let buf = [0x00; 7];
        assert!(memread::<u64>(&buf, 0, Endian::Little).is_err());
    }

    // -- safe memwrite --

    #[test]
    fn memwrite_uint8_range_ok() {
        let mut buf = [0u8; 3];
        assert!(memwrite(&mut buf, 1, 0x42u8, Endian::Little).is_ok());
        assert_eq!(buf, [0, 0x42, 0]);
    }

    #[test]
    fn memwrite_uint8_range_oob() {
        let mut buf = [0u8; 1];
        assert!(memwrite(&mut buf, 1, 0x42u8, Endian::Little).is_err());
    }

    #[test]
    fn memwrite_uint16_range_ok() {
        let mut buf = [0u8; 4];
        assert!(memwrite::<u16>(&mut buf, 1, 0x1234, Endian::Little).is_ok());
        assert_eq!(buf, [0, 0x34, 0x12, 0]);
    }

    #[test]
    fn memwrite_uint16_range_oob() {
        let mut buf = [0u8; 2];
        assert!(memwrite::<u16>(&mut buf, 1, 0x1234, Endian::Little).is_err());
    }

    #[test]
    fn memwrite_uint32_range_ok() {
        let mut buf = [0u8; 6];
        assert!(memwrite::<u32>(&mut buf, 1, 0x12345678, Endian::Big).is_ok());
        assert_eq!(buf, [0, 0x12, 0x34, 0x56, 0x78, 0]);
    }

    #[test]
    fn memwrite_uint32_range_oob() {
        let mut buf = [0u8; 3];
        assert!(memwrite::<u32>(&mut buf, 0, 0x12345678, Endian::Big).is_err());
    }

    #[test]
    fn memwrite_uint64_range_ok() {
        let mut buf = [0u8; 10];
        assert!(memwrite::<u64>(&mut buf, 1, 0x1234567890ABCDEF, Endian::Big).is_ok());
        assert_eq!(buf, [0, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0]);
    }

    #[test]
    fn memwrite_uint64_range_oob() {
        let mut buf = [0u8; 7];
        assert!(memwrite::<u64>(&mut buf, 0, 0x1234567890ABCDEF, Endian::Big).is_err());
    }

    // -- roundtrip tests --

    #[test]
    fn roundtrip_uint16() {
        for endian in [Endian::Big, Endian::Little] {
            let mut buf = [0u8; 2];
            memwrite(&mut buf, 0, 0xCAFEu16, endian).unwrap();
            assert_eq!(memread::<u16>(&buf, 0, endian).unwrap(), 0xCAFE);
        }
    }

    #[test]
    fn roundtrip_uint24() {
        for endian in [Endian::Big, Endian::Little] {
            let mut buf = [0u8; 3];
            memwrite(&mut buf, 0, U24::new(0xABCDEF), endian).unwrap();
            assert_eq!(memread::<U24>(&buf, 0, endian).unwrap().value(), 0xABCDEF);
        }
    }

    #[test]
    fn roundtrip_uint32() {
        for endian in [Endian::Big, Endian::Little] {
            let mut buf = [0u8; 4];
            memwrite(&mut buf, 0, 0xDEADBEEFu32, endian).unwrap();
            assert_eq!(memread::<u32>(&buf, 0, endian).unwrap(), 0xDEADBEEF);
        }
    }

    #[test]
    fn roundtrip_uint64() {
        for endian in [Endian::Big, Endian::Little] {
            let mut buf = [0u8; 8];
            memwrite(&mut buf, 0, 0xDEADBEEFCAFEBABEu64, endian).unwrap();
            assert_eq!(memread::<u64>(&buf, 0, endian).unwrap(), 0xDEADBEEFCAFEBABE);
        }
    }
}
