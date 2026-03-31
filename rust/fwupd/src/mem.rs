//! Safe memory read/write utilities with bounds checking and endian support.
//!
//! This crate provides:
//! - Bounds-checked memory read/write operations (`memchk_read`, `memchk_write`, `memcpy_safe`)
//! - Endian-aware integer read/write to byte buffers (`memread_uint16`, `memwrite_uint32`, etc.)
//! - Safe variants with bounds checking (`memread_uint16_safe`, etc.)
//! - Needle-in-haystack search (`memmem_safe`)
//! - Buffer comparison (`memcmp_safe`)
//! - Safe memory duplication (`memdup_safe`)
//! - ASCII string extraction from byte buffers (`memstrsafe`)


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
pub fn memchk_read(bufsz: usize, offset: usize, n: usize) -> Result<(), MemError> {
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
pub fn memchk_write(bufsz: usize, offset: usize, n: usize) -> Result<(), MemError> {
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
pub fn memcpy_safe(
    dst: &mut [u8],
    dst_offset: usize,
    src: &[u8],
    src_offset: usize,
    n: usize,
) -> Result<(), MemError> {
    memchk_read(src.len(), src_offset, n)?;
    memchk_write(dst.len(), dst_offset, n)?;
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
pub fn memcmp_safe(
    buf1: &[u8],
    buf1_offset: usize,
    buf2: &[u8],
    buf2_offset: usize,
    n: usize,
) -> Result<(), MemError> {
    memchk_read(buf1.len(), buf1_offset, n)?;
    memchk_read(buf2.len(), buf2_offset, n)?;
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
pub fn memmem_safe(haystack: &[u8], needle: &[u8]) -> Result<usize, MemError> {
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
pub fn memdup_safe(src: &[u8], n: usize) -> Result<Vec<u8>, MemError> {
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
// Safe memstrsafe (extract ASCII string)
// ---------------------------------------------------------------------------

/// Extract an ASCII string from `buf[offset..offset+maxsz]`.
///
/// Non-printable characters are stripped. Returns `Err` if the region is out
/// of bounds or contains no valid ASCII characters.
pub fn memstrsafe(buf: &[u8], offset: usize, maxsz: usize) -> Result<String, MemError> {
    memchk_read(buf.len(), offset, maxsz)?;
    let region = &buf[offset..offset + maxsz];
    // Replicate fu_strsafe: keep only printable ASCII, trim, return None if empty
    let s: String = region
        .iter()
        .take_while(|&&b| b != 0) // stop at NUL
        .filter(|&&b| b >= 0x20 && b <= 0x7e) // printable ASCII
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
/// `u8` and 24-bit values are handled separately since `u8` has no endian
/// parameter and there is no native 24-bit integer type.
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

/// Read a value of type `T` from `buf` with the given endianness.
pub fn memread<T: MemReadWrite>(buf: &[u8], endian: Endian) -> T {
    T::mem_read(buf, endian)
}

/// Write a value of type `T` to `buf` with the given endianness.
pub fn memwrite<T: MemReadWrite>(buf: &mut [u8], val: T, endian: Endian) {
    T::mem_write(buf, val, endian);
}

/// Bounds-checked read of a value of type `T` from `buf` at `offset`.
///
/// Prefer [`memread`] in obviously correct cases or when performance matters.
/// Use this function when the offset comes from untrusted data (e.g. device
/// firmware) and an out-of-bounds read must be handled gracefully.
pub fn memread_safe<T: MemReadWrite>(
    buf: &[u8],
    offset: usize,
    endian: Endian,
) -> Result<T, MemError> {
    memchk_read(buf.len(), offset, T::SIZE)?;
    Ok(T::mem_read(&buf[offset..], endian))
}

/// Bounds-checked write of a value of type `T` to `buf` at `offset`.
///
/// Prefer [`memwrite`] in obviously correct cases or when performance matters.
/// Use this function when the offset comes from untrusted data (e.g. device
/// firmware) and an out-of-bounds write must be handled gracefully.
pub fn memwrite_safe<T: MemReadWrite>(
    buf: &mut [u8],
    offset: usize,
    value: T,
    endian: Endian,
) -> Result<(), MemError> {
    memchk_write(buf.len(), offset, T::SIZE)?;
    T::mem_write(&mut buf[offset..], value, endian);
    Ok(())
}

// Convenience aliases matching the C function names.

/// Read a `u16` from `buf` with the given endianness.
pub fn memread_uint16(buf: &[u8], endian: Endian) -> u16 {
    memread::<u16>(buf, endian)
}

/// Read a `u32` from `buf` with the given endianness.
pub fn memread_uint32(buf: &[u8], endian: Endian) -> u32 {
    memread::<u32>(buf, endian)
}

/// Read a `u64` from `buf` with the given endianness.
pub fn memread_uint64(buf: &[u8], endian: Endian) -> u64 {
    memread::<u64>(buf, endian)
}

/// Write a `u16` to `buf` with the given endianness.
pub fn memwrite_uint16(buf: &mut [u8], val: u16, endian: Endian) {
    memwrite(buf, val, endian);
}

/// Write a `u32` to `buf` with the given endianness.
pub fn memwrite_uint32(buf: &mut [u8], val: u32, endian: Endian) {
    memwrite(buf, val, endian);
}

/// Write a `u64` to `buf` with the given endianness.
pub fn memwrite_uint64(buf: &mut [u8], val: u64, endian: Endian) {
    memwrite(buf, val, endian);
}

// 24-bit: no native type, handled separately.

/// Read a 24-bit value from `buf` with the given endianness.
///
/// Returns a `u32` with the upper 8 bits zeroed.
pub fn memread_uint24(buf: &[u8], endian: Endian) -> u32 {
    match endian {
        Endian::Big => (buf[0] as u32) << 16 | (buf[1] as u32) << 8 | (buf[2] as u32),
        Endian::Little => (buf[0] as u32) | (buf[1] as u32) << 8 | (buf[2] as u32) << 16,
    }
}

/// Write a 24-bit value to `buf` with the given endianness.
///
/// Only the lower 24 bits of `val` are written.
pub fn memwrite_uint24(buf: &mut [u8], val: u32, endian: Endian) {
    match endian {
        Endian::Big => {
            buf[0] = (val >> 16) as u8;
            buf[1] = (val >> 8) as u8;
            buf[2] = val as u8;
        }
        Endian::Little => {
            buf[0] = val as u8;
            buf[1] = (val >> 8) as u8;
            buf[2] = (val >> 16) as u8;
        }
    }
}

// ---------------------------------------------------------------------------
// Safe endian-aware memread/memwrite (bounds-checked)
// ---------------------------------------------------------------------------

/// Bounds-checked read of a `u8` from `buf` at `offset`.
pub fn memread_uint8_safe(buf: &[u8], offset: usize) -> Result<u8, MemError> {
    memchk_read(buf.len(), offset, 1)?;
    Ok(buf[offset])
}

/// Bounds-checked read of a `u16` from `buf` at `offset` with the given endianness.
pub fn memread_uint16_safe(buf: &[u8], offset: usize, endian: Endian) -> Result<u16, MemError> {
    memread_safe(buf, offset, endian)
}

/// Bounds-checked read of a 24-bit value from `buf` at `offset` with the given endianness.
pub fn memread_uint24_safe(buf: &[u8], offset: usize, endian: Endian) -> Result<u32, MemError> {
    memchk_read(buf.len(), offset, 3)?;
    Ok(memread_uint24(&buf[offset..], endian))
}

/// Bounds-checked read of a `u32` from `buf` at `offset` with the given endianness.
pub fn memread_uint32_safe(buf: &[u8], offset: usize, endian: Endian) -> Result<u32, MemError> {
    memread_safe(buf, offset, endian)
}

/// Bounds-checked read of a `u64` from `buf` at `offset` with the given endianness.
pub fn memread_uint64_safe(buf: &[u8], offset: usize, endian: Endian) -> Result<u64, MemError> {
    memread_safe(buf, offset, endian)
}

/// Bounds-checked write of a `u8` to `buf` at `offset`.
pub fn memwrite_uint8_safe(buf: &mut [u8], offset: usize, value: u8) -> Result<(), MemError> {
    memchk_write(buf.len(), offset, 1)?;
    buf[offset] = value;
    Ok(())
}

/// Bounds-checked write of a `u16` to `buf` at `offset` with the given endianness.
pub fn memwrite_uint16_safe(
    buf: &mut [u8],
    offset: usize,
    value: u16,
    endian: Endian,
) -> Result<(), MemError> {
    memwrite_safe(buf, offset, value, endian)
}

/// Bounds-checked write of a `u32` to `buf` at `offset` with the given endianness.
pub fn memwrite_uint32_safe(
    buf: &mut [u8],
    offset: usize,
    value: u32,
    endian: Endian,
) -> Result<(), MemError> {
    memwrite_safe(buf, offset, value, endian)
}

/// Bounds-checked write of a `u64` to `buf` at `offset` with the given endianness.
pub fn memwrite_uint64_safe(
    buf: &mut [u8],
    offset: usize,
    value: u64,
    endian: Endian,
) -> Result<(), MemError> {
    memwrite_safe(buf, offset, value, endian)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    // -- memchk_read --

    #[test]
    fn memchk_read_zero_n() {
        assert!(memchk_read(0, 0, 0).is_ok());
        assert!(memchk_read(10, 5, 0).is_ok());
    }

    #[test]
    fn memchk_read_ok() {
        assert!(memchk_read(10, 0, 10).is_ok());
        assert!(memchk_read(10, 5, 5).is_ok());
    }

    #[test]
    fn memchk_read_n_exceeds_bufsz() {
        assert!(memchk_read(5, 0, 10).is_err());
    }

    #[test]
    fn memchk_read_offset_overflow() {
        assert!(memchk_read(10, usize::MAX, 1).is_err());
    }

    #[test]
    fn memchk_read_past_end() {
        assert!(memchk_read(10, 8, 5).is_err());
    }

    // -- memchk_write --

    #[test]
    fn memchk_write_zero_n() {
        assert!(memchk_write(0, 0, 0).is_ok());
    }

    #[test]
    fn memchk_write_ok() {
        assert!(memchk_write(10, 0, 10).is_ok());
    }

    #[test]
    fn memchk_write_n_exceeds_bufsz() {
        assert!(memchk_write(5, 0, 10).is_err());
    }

    #[test]
    fn memchk_write_offset_overflow() {
        assert!(memchk_write(10, usize::MAX, 1).is_err());
    }

    #[test]
    fn memchk_write_past_end() {
        assert!(memchk_write(10, 8, 5).is_err());
    }

    // -- memcpy_safe --

    #[test]
    fn memcpy_safe_basic() {
        let src = [1, 2, 3, 4, 5];
        let mut dst = [0u8; 5];
        assert!(memcpy_safe(&mut dst, 0, &src, 0, 5).is_ok());
        assert_eq!(dst, [1, 2, 3, 4, 5]);
    }

    #[test]
    fn memcpy_safe_with_offsets() {
        let src = [10, 20, 30, 40, 50];
        let mut dst = [0u8; 10];
        assert!(memcpy_safe(&mut dst, 3, &src, 1, 3).is_ok());
        assert_eq!(dst, [0, 0, 0, 20, 30, 40, 0, 0, 0, 0]);
    }

    #[test]
    fn memcpy_safe_src_oob() {
        let src = [1, 2, 3];
        let mut dst = [0u8; 10];
        assert!(memcpy_safe(&mut dst, 0, &src, 2, 5).is_err());
    }

    #[test]
    fn memcpy_safe_dst_oob() {
        let src = [1, 2, 3, 4, 5];
        let mut dst = [0u8; 3];
        assert!(memcpy_safe(&mut dst, 0, &src, 0, 5).is_err());
    }

    #[test]
    fn memcpy_safe_zero_bytes() {
        let src = [1, 2, 3];
        let mut dst = [0u8; 3];
        assert!(memcpy_safe(&mut dst, 0, &src, 0, 0).is_ok());
        assert_eq!(dst, [0, 0, 0]);
    }

    // -- memcmp_safe --

    #[test]
    fn memcmp_safe_equal() {
        let a = [1, 2, 3, 4, 5];
        let b = [1, 2, 3, 4, 5];
        assert!(memcmp_safe(&a, 0, &b, 0, 5).is_ok());
    }

    #[test]
    fn memcmp_safe_partial_equal() {
        let a = [0, 0, 1, 2, 3];
        let b = [1, 2, 3, 0, 0];
        assert!(memcmp_safe(&a, 2, &b, 0, 3).is_ok());
    }

    #[test]
    fn memcmp_safe_mismatch() {
        let a = [1, 2, 3];
        let b = [1, 2, 4];
        let err = memcmp_safe(&a, 0, &b, 0, 3).unwrap_err();
        assert!(err.message.contains("0x03"));
        assert!(err.message.contains("0x04"));
    }

    #[test]
    fn memcmp_safe_oob() {
        let a = [1, 2, 3];
        let b = [1, 2, 3, 4, 5];
        assert!(memcmp_safe(&a, 0, &b, 0, 5).is_err());
    }

    // -- memmem_safe --

    #[test]
    fn memmem_safe_found() {
        let haystack = [0, 1, 2, 3, 4, 5, 6];
        let needle = [3, 4, 5];
        assert_eq!(memmem_safe(&haystack, &needle), Ok(3));
    }

    #[test]
    fn memmem_safe_at_start() {
        let haystack = [1, 2, 3, 4, 5];
        let needle = [1, 2];
        assert_eq!(memmem_safe(&haystack, &needle), Ok(0));
    }

    #[test]
    fn memmem_safe_at_end() {
        let haystack = [1, 2, 3, 4, 5];
        let needle = [4, 5];
        assert_eq!(memmem_safe(&haystack, &needle), Ok(3));
    }

    #[test]
    fn memmem_safe_not_found() {
        let haystack = [1, 2, 3, 4, 5];
        let needle = [6, 7];
        assert!(memmem_safe(&haystack, &needle).is_err());
    }

    #[test]
    fn memmem_safe_needle_too_large() {
        let haystack = [1, 2];
        let needle = [1, 2, 3, 4, 5];
        assert!(memmem_safe(&haystack, &needle).is_err());
    }

    #[test]
    fn memmem_safe_empty_needle() {
        let haystack = [1, 2, 3];
        assert_eq!(memmem_safe(&haystack, &[]), Ok(0));
    }

    // -- memdup_safe --

    #[test]
    fn memdup_safe_basic() {
        let src = [1, 2, 3, 4, 5];
        let result = memdup_safe(&src, 5).unwrap();
        assert_eq!(result, vec![1, 2, 3, 4, 5]);
    }

    #[test]
    fn memdup_safe_partial() {
        let src = [10, 20, 30, 40, 50];
        let result = memdup_safe(&src, 3).unwrap();
        assert_eq!(result, vec![10, 20, 30]);
    }

    #[test]
    fn memdup_safe_too_large() {
        let src = [0u8; 4];
        assert!(memdup_safe(&src, 0x40000001).is_err());
    }

    // -- memstrsafe --

    #[test]
    fn memstrsafe_basic() {
        let buf = b"Hello\x00World";
        let result = memstrsafe(buf, 0, 11).unwrap();
        assert_eq!(result, "Hello");
    }

    #[test]
    fn memstrsafe_with_offset() {
        let buf = b"\x00\x00Hi there\x00";
        let result = memstrsafe(buf, 2, 8).unwrap();
        assert_eq!(result, "Hi there");
    }

    #[test]
    fn memstrsafe_non_printable_stripped() {
        let buf = [0x01, 0x02, b'A', b'B', 0x7f, b'C', 0x00];
        let result = memstrsafe(&buf, 0, 7).unwrap();
        assert_eq!(result, "ABC");
    }

    #[test]
    fn memstrsafe_empty() {
        let buf = [0x00, 0x01, 0x02];
        assert!(memstrsafe(&buf, 0, 3).is_err());
    }

    #[test]
    fn memstrsafe_oob() {
        let buf = [b'A', b'B'];
        assert!(memstrsafe(&buf, 1, 5).is_err());
    }

    // -- memread/memwrite uint16 --

    #[test]
    fn memread_uint16_le() {
        let buf = [0x34, 0x12];
        assert_eq!(memread_uint16(&buf, Endian::Little), 0x1234);
    }

    #[test]
    fn memread_uint16_be() {
        let buf = [0x12, 0x34];
        assert_eq!(memread_uint16(&buf, Endian::Big), 0x1234);
    }

    #[test]
    fn memwrite_uint16_le() {
        let mut buf = [0u8; 2];
        memwrite_uint16(&mut buf, 0x1234, Endian::Little);
        assert_eq!(buf, [0x34, 0x12]);
    }

    #[test]
    fn memwrite_uint16_be() {
        let mut buf = [0u8; 2];
        memwrite_uint16(&mut buf, 0x1234, Endian::Big);
        assert_eq!(buf, [0x12, 0x34]);
    }

    // -- memread/memwrite uint24 --

    #[test]
    fn memread_uint24_le() {
        let buf = [0x56, 0x34, 0x12];
        assert_eq!(memread_uint24(&buf, Endian::Little), 0x123456);
    }

    #[test]
    fn memread_uint24_be() {
        let buf = [0x12, 0x34, 0x56];
        assert_eq!(memread_uint24(&buf, Endian::Big), 0x123456);
    }

    #[test]
    fn memwrite_uint24_le() {
        let mut buf = [0u8; 3];
        memwrite_uint24(&mut buf, 0x123456, Endian::Little);
        assert_eq!(buf, [0x56, 0x34, 0x12]);
    }

    #[test]
    fn memwrite_uint24_be() {
        let mut buf = [0u8; 3];
        memwrite_uint24(&mut buf, 0x123456, Endian::Big);
        assert_eq!(buf, [0x12, 0x34, 0x56]);
    }

    // -- memread/memwrite uint32 --

    #[test]
    fn memread_uint32_le() {
        let buf = [0x78, 0x56, 0x34, 0x12];
        assert_eq!(memread_uint32(&buf, Endian::Little), 0x12345678);
    }

    #[test]
    fn memread_uint32_be() {
        let buf = [0x12, 0x34, 0x56, 0x78];
        assert_eq!(memread_uint32(&buf, Endian::Big), 0x12345678);
    }

    #[test]
    fn memwrite_uint32_le() {
        let mut buf = [0u8; 4];
        memwrite_uint32(&mut buf, 0x12345678, Endian::Little);
        assert_eq!(buf, [0x78, 0x56, 0x34, 0x12]);
    }

    #[test]
    fn memwrite_uint32_be() {
        let mut buf = [0u8; 4];
        memwrite_uint32(&mut buf, 0x12345678, Endian::Big);
        assert_eq!(buf, [0x12, 0x34, 0x56, 0x78]);
    }

    // -- memread/memwrite uint64 --

    #[test]
    fn memread_uint64_le() {
        let buf = [0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12];
        assert_eq!(memread_uint64(&buf, Endian::Little), 0x1234567890ABCDEF);
    }

    #[test]
    fn memread_uint64_be() {
        let buf = [0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF];
        assert_eq!(memread_uint64(&buf, Endian::Big), 0x1234567890ABCDEF);
    }

    #[test]
    fn memwrite_uint64_le() {
        let mut buf = [0u8; 8];
        memwrite_uint64(&mut buf, 0x1234567890ABCDEF, Endian::Little);
        assert_eq!(buf, [0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12]);
    }

    #[test]
    fn memwrite_uint64_be() {
        let mut buf = [0u8; 8];
        memwrite_uint64(&mut buf, 0x1234567890ABCDEF, Endian::Big);
        assert_eq!(buf, [0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF]);
    }

    // -- safe memread --

    #[test]
    fn memread_uint8_safe_ok() {
        let buf = [0x42, 0x43, 0x44];
        assert_eq!(memread_uint8_safe(&buf, 1), Ok(0x43));
    }

    #[test]
    fn memread_uint8_safe_oob() {
        let buf = [0x42];
        assert!(memread_uint8_safe(&buf, 1).is_err());
    }

    #[test]
    fn memread_uint16_safe_ok() {
        let buf = [0x00, 0x34, 0x12, 0x00];
        assert_eq!(memread_uint16_safe(&buf, 1, Endian::Little), Ok(0x1234));
    }

    #[test]
    fn memread_uint16_safe_oob() {
        let buf = [0x34];
        assert!(memread_uint16_safe(&buf, 0, Endian::Little).is_err());
    }

    #[test]
    fn memread_uint24_safe_ok() {
        let buf = [0x00, 0x56, 0x34, 0x12, 0x00];
        assert_eq!(memread_uint24_safe(&buf, 1, Endian::Little), Ok(0x123456));
    }

    #[test]
    fn memread_uint24_safe_oob() {
        let buf = [0x56, 0x34];
        assert!(memread_uint24_safe(&buf, 0, Endian::Little).is_err());
    }

    #[test]
    fn memread_uint32_safe_ok() {
        let buf = [0x78, 0x56, 0x34, 0x12];
        assert_eq!(memread_uint32_safe(&buf, 0, Endian::Little), Ok(0x12345678));
    }

    #[test]
    fn memread_uint32_safe_oob() {
        let buf = [0x78, 0x56, 0x34];
        assert!(memread_uint32_safe(&buf, 0, Endian::Little).is_err());
    }

    #[test]
    fn memread_uint64_safe_ok() {
        let buf = [0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12];
        assert_eq!(
            memread_uint64_safe(&buf, 0, Endian::Little),
            Ok(0x1234567890ABCDEF)
        );
    }

    #[test]
    fn memread_uint64_safe_oob() {
        let buf = [0x00; 7];
        assert!(memread_uint64_safe(&buf, 0, Endian::Little).is_err());
    }

    // -- safe memwrite --

    #[test]
    fn memwrite_uint8_safe_ok() {
        let mut buf = [0u8; 3];
        assert!(memwrite_uint8_safe(&mut buf, 1, 0x42).is_ok());
        assert_eq!(buf, [0, 0x42, 0]);
    }

    #[test]
    fn memwrite_uint8_safe_oob() {
        let mut buf = [0u8; 1];
        assert!(memwrite_uint8_safe(&mut buf, 1, 0x42).is_err());
    }

    #[test]
    fn memwrite_uint16_safe_ok() {
        let mut buf = [0u8; 4];
        assert!(memwrite_uint16_safe(&mut buf, 1, 0x1234, Endian::Little).is_ok());
        assert_eq!(buf, [0, 0x34, 0x12, 0]);
    }

    #[test]
    fn memwrite_uint16_safe_oob() {
        let mut buf = [0u8; 2];
        assert!(memwrite_uint16_safe(&mut buf, 1, 0x1234, Endian::Little).is_err());
    }

    #[test]
    fn memwrite_uint32_safe_ok() {
        let mut buf = [0u8; 6];
        assert!(memwrite_uint32_safe(&mut buf, 1, 0x12345678, Endian::Big).is_ok());
        assert_eq!(buf, [0, 0x12, 0x34, 0x56, 0x78, 0]);
    }

    #[test]
    fn memwrite_uint32_safe_oob() {
        let mut buf = [0u8; 3];
        assert!(memwrite_uint32_safe(&mut buf, 0, 0x12345678, Endian::Big).is_err());
    }

    #[test]
    fn memwrite_uint64_safe_ok() {
        let mut buf = [0u8; 10];
        assert!(memwrite_uint64_safe(&mut buf, 1, 0x1234567890ABCDEF, Endian::Big).is_ok());
        assert_eq!(buf, [0, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0]);
    }

    #[test]
    fn memwrite_uint64_safe_oob() {
        let mut buf = [0u8; 7];
        assert!(memwrite_uint64_safe(&mut buf, 0, 0x1234567890ABCDEF, Endian::Big).is_err());
    }

    // -- roundtrip tests --

    #[test]
    fn roundtrip_uint16() {
        for endian in [Endian::Big, Endian::Little] {
            let mut buf = [0u8; 2];
            memwrite_uint16(&mut buf, 0xCAFE, endian);
            assert_eq!(memread_uint16(&buf, endian), 0xCAFE);
        }
    }

    #[test]
    fn roundtrip_uint24() {
        for endian in [Endian::Big, Endian::Little] {
            let mut buf = [0u8; 3];
            memwrite_uint24(&mut buf, 0xABCDEF, endian);
            assert_eq!(memread_uint24(&buf, endian), 0xABCDEF);
        }
    }

    #[test]
    fn roundtrip_uint32() {
        for endian in [Endian::Big, Endian::Little] {
            let mut buf = [0u8; 4];
            memwrite_uint32(&mut buf, 0xDEADBEEF, endian);
            assert_eq!(memread_uint32(&buf, endian), 0xDEADBEEF);
        }
    }

    #[test]
    fn roundtrip_uint64() {
        for endian in [Endian::Big, Endian::Little] {
            let mut buf = [0u8; 8];
            memwrite_uint64(&mut buf, 0xDEADBEEFCAFEBABE, endian);
            assert_eq!(memread_uint64(&buf, endian), 0xDEADBEEFCAFEBABE);
        }
    }
}
