/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Endian-aware integer wrappers.
//!
//! [`BigEndian<T>`] and [`LittleEndian<T>`] are zero-overhead wrappers around
//! primitive integer types that enforce byte order at the type level.  The
//! stored value is always kept in the named byte order, so casting a pointer
//! directly to the struct (e.g. from a `#[repr(C)]` struct) gives the correct
//! wire representation on any host.
//!
//! And of course the types prevent our APIs from accidentally taking a known fixed
//! endian type where a native type is wanted and vice versa (e.g. passing little
//! endian CAB data through an API on a big endian host).
//!
//! This is a simple minimal version, any further requirements should be
//! resolved by adding [zerocopy](https://docs.rs/zerocopy/latest/zerocopy/index.html)
//! as dependency instead.
//!
//! # Constructing values
//!
//! ```
//! use fwupd::endian::{BigEndian, LittleEndian};
//!
//! // From a native (host-endian) integer:
//! let be = BigEndian::<u32>::new(0x1234_5678);
//! let le = LittleEndian::<u32>::new(0x1234_5678);
//!
//! // From raw bytes already in the named byte order:
//! let be = BigEndian::<u32>::from_bytes(&[0x12, 0x34, 0x56, 0x78]);
//! let le = LittleEndian::<u32>::from_bytes(&[0x78, 0x56, 0x34, 0x12]);
//! assert_eq!(be.get(), le.get()); // both yield 0x1234_5678
//! ```
//!
//! # Reading and writing
//!
//! ```
//! use fwupd::endian::{BigEndian, LittleEndian, ToBytes};
//!
//! let be = BigEndian::<u16>::new(0xABCD);
//!
//! // get() always returns the value in host-native byte order:
//! assert_eq!(be.get(), 0xABCD_u16);
//!
//! // to_bytes() returns a fixed-size array in the type's byte order —
//! // the size is guaranteed at compile time, no runtime check needed:
//! assert_eq!(be.to_bytes(), [0xAB, 0xCD]);
//!
//! let le = LittleEndian::<u16>::new(0xABCD);
//! assert_eq!(le.to_bytes(), [0xCD, 0xAB]);
//! ```
//!
//! # Parsing from a byte stream
//!
//! Short slices are zero-padded toward the most-significant byte:
//!
//! ```
//! use fwupd::endian::LittleEndian;
//!
//! let le = LittleEndian::<u32>::from_bytes(&[0x78, 0x56]); // 2 bytes → u32
//! assert_eq!(le.get(), 0x0000_5678_u32);
//! ```
//!
//! # Direct struct cast from a byte buffer
//!
//! Because both wrappers are `#[repr(transparent)]`, they have the same memory
//! layout as their inner type and compose naturally into `#[repr(C)]` structs.
//! Combined with `packed`, every field sits at its wire offset and the struct
//! can be overlaid on a raw byte slice with a single pointer cast:
//!
//! ```
//! use fwupd::endian::LittleEndian;
//!
//! /// You are responsible for correct alignment of the types!
//! #[repr(C, packed)]
//! struct CabHeader {
//!     signature:    [u8; 4],
//!     cabinet_size: LittleEndian<u32>,
//!     files_offset: LittleEndian<u32>,
//! }
//!
//! fn parse(data: &[u8]) -> Option<&CabHeader> {
//!     if data.len() < std::mem::size_of::<CabHeader>() {
//!         return None;
//!     }
//!     // SAFETY: data is large enough; packed removes alignment requirements;
//!     //         all bit patterns are valid for integer fields.
//!     Some(unsafe { &*(data.as_ptr() as *const CabHeader) })
//! }
//!
//! let buf: &[u8] = &[b'M', b'S', b'C', b'F',  // signature
//!                     0x20, 0x00, 0x00, 0x00,   // cabinet_size = 32 LE
//!                     0x10, 0x00, 0x00, 0x00];  // files_offset = 16 LE
//! let hdr = parse(buf).unwrap();
//! assert_eq!(hdr.cabinet_size.get(), 32_u32);
//! assert_eq!(hdr.files_offset.get(), 16_u32);
//! ```

/// Typesafe wrapper around the primitive integer types to
/// ensure endianness. This wrapper always stores the data
/// in its endianness, the data can be converted to/from
/// native byte order with the various methods.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct BigEndian<T>(T);

/// Typesafe wrapper around the primitive integer types to
/// ensure endianness. This wrapper always stores the data
/// in its endianness, the data can be converted to/from
/// native byte order with the various methods.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct LittleEndian<T>(T);

impl<T: PrimitiveInteger + PartialOrd> PartialOrd for BigEndian<T> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.get().partial_cmp(&other.get())
    }
}

impl<T: PrimitiveInteger + Ord> Ord for BigEndian<T> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.get().cmp(&other.get())
    }
}

impl<T: PrimitiveInteger + PartialOrd> PartialOrd for LittleEndian<T> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.get().partial_cmp(&other.get())
    }
}

impl<T: PrimitiveInteger + Ord> Ord for LittleEndian<T> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.get().cmp(&other.get())
    }
}

/// Convert a endian-specific type to its correct raw byte
/// representation.
pub trait ToBytes {
    type Bytes: AsRef<[u8]>;
    fn to_bytes(self) -> Self::Bytes;
}

/// Low-level byte-order operations on a primitive integer type.
///
/// Implemented for all standard integer primitives (`u8`–`u128`, `i8`–`i128`,
/// `usize`, `isize`).  You will rarely call these methods directly; they are
/// the machinery that [`BigEndian<T>`] and [`LittleEndian<T>`] delegate to.
///
/// The `to_*` / `from_*` pairs are inverses: a value reinterpreted into a
/// byte order and then back out yields the original.  On a host whose native
/// byte order matches the named order the conversion is a no-op.
pub trait PrimitiveInteger: Copy {
    /// Fixed-size byte array for this type (e.g. `[u8; 4]` for `u32`).
    type Bytes: AsRef<[u8]>;

    /// Reinterpret `self` as a big-endian stored value.
    #[must_use]
    fn to_big_endian(self) -> Self;

    /// Interpret a big-endian stored value as a native-endian value.
    #[must_use]
    fn from_big_endian(value: Self) -> Self;

    /// Reinterpret `self` as a little-endian stored value.
    #[must_use]
    fn to_little_endian(self) -> Self;

    /// Interpret a little-endian stored value as a native-endian value.
    #[must_use]
    fn from_little_endian(value: Self) -> Self;

    /// Parse up to `size_of::<Self>()` bytes in big-endian order.
    /// Shorter slices are zero-padded in the most-significant positions.
    fn from_be_slice(bytes: &[u8]) -> Self;

    /// Parse up to `size_of::<Self>()` bytes in little-endian order.
    /// Shorter slices are zero-padded in the most-significant positions.
    fn from_le_slice(bytes: &[u8]) -> Self;

    /// Write up to `bytes.len()` bytes of `self` in big-endian order into `bytes`.
    fn to_be_slice(self, bytes: &mut [u8]);

    /// Write up to `bytes.len()` bytes of `self` in little-endian order into `bytes`.
    fn to_le_slice(self, bytes: &mut [u8]);

    /// Return `self` as a fixed-size array in big-endian byte order.
    fn to_be_arr(self) -> Self::Bytes;

    /// Return `self` as a fixed-size array in little-endian byte order.
    fn to_le_arr(self) -> Self::Bytes;
}

macro_rules! impl_primitive_int {
    ($($t:ty),*) => {$(
            impl PrimitiveInteger for $t {
                type Bytes = [u8; std::mem::size_of::<$t>()];
                #[inline]
                fn to_big_endian(self) -> Self {
                    self.to_be()
                }
                #[inline]
                fn from_big_endian(value: $t) -> Self {
                    Self::from_be(value)
                }
                #[inline]
                fn to_little_endian(self) -> Self {
                    self.to_le()
                }
                #[inline]
                fn from_little_endian(value: $t) -> Self {
                    Self::from_le(value)
                }
                #[inline]
                fn from_be_slice(bytes: &[u8]) -> Self {
                    let mut buf = [0u8; std::mem::size_of::<$t>()];
                    let sz = std::mem::size_of::<$t>();
                    let n = bytes.len().min(sz);
                    buf[sz - n..].copy_from_slice(&bytes[..n]);
                    Self::from_be_bytes(buf)
                }
                #[inline]
                fn from_le_slice(bytes: &[u8]) -> Self {
                    let mut buf = [0u8; std::mem::size_of::<$t>()];
                    let n = bytes.len().min(buf.len());
                    buf[..n].copy_from_slice(&bytes[..n]);
                    Self::from_le_bytes(buf)
                }
                #[inline]
                fn to_be_slice(self, bytes: &mut [u8]) {
                    let buf = self.to_be_bytes();
                    let n = bytes.len().min(buf.len());
                    bytes[..n].copy_from_slice(&buf[..n]);
                }
                #[inline]
                fn to_le_slice(self, bytes: &mut [u8]) {
                    let buf = self.to_le_bytes();
                    let n = bytes.len().min(buf.len());
                    bytes[..n].copy_from_slice(&buf[..n]);
                }
                #[inline]
                fn to_be_arr(self) -> Self::Bytes {
                    self.to_be_bytes()
                }
                #[inline]
                fn to_le_arr(self) -> Self::Bytes {
                    self.to_le_bytes()
                }
            }
        )*
    };
}

impl_primitive_int! {u8, i8, u16, i16, u32, i32, u64, i64, u128, i128, usize, isize}

impl<T: PrimitiveInteger> ToBytes for BigEndian<T> {
    type Bytes = T::Bytes;
    fn to_bytes(self) -> T::Bytes {
        self.get().to_be_arr()
    }
}

impl<T: PrimitiveInteger> ToBytes for LittleEndian<T> {
    type Bytes = T::Bytes;
    fn to_bytes(self) -> T::Bytes {
        self.get().to_le_arr()
    }
}

impl<T: PrimitiveInteger> BigEndian<T> {
    /// Creates a new instance of the native byte order type `T`
    /// in big endian order.
    pub fn new(native: T) -> Self {
        Self(native.to_big_endian())
    }

    /// Creates a new instance of the raw type `T` in big endian order.
    /// The caller must ensure that the raw value is in big endian.
    pub fn from_raw(raw: T) -> Self {
        Self(raw)
    }
    /// Parse up to `size_of::<T>()` bytes in big-endian order; shorter
    /// slices are zero-padded in the most-significant positions.
    #[must_use]
    pub fn from_bytes(bytes: &[u8]) -> Self {
        Self::new(T::from_be_slice(bytes))
    }

    /// Return the value of self in the host's native byte order
    pub fn get(self) -> T {
        T::from_big_endian(self.0)
    }

    /// Return the raw big endian value of self
    pub fn raw(self) -> T {
        self.0
    }
}

impl<T: PrimitiveInteger> LittleEndian<T> {
    /// Creates a new instance of the native byte order type `T`
    /// in little endian order.
    pub fn new(value: T) -> Self {
        Self(value.to_little_endian())
    }

    /// Creates a new instance of the raw type `T` in little endian order.
    /// The caller must ensure that the raw value is in little endian.
    pub fn from_raw(raw: T) -> Self {
        Self(raw)
    }

    /// Parse up to `size_of::<T>()` bytes in little-endian order; shorter
    /// slices are zero-padded in the most-significant positions.
    #[must_use]
    pub fn from_bytes(bytes: &[u8]) -> Self {
        Self::new(T::from_le_slice(bytes))
    }

    /// Return the value of self in the host's native byte order
    pub fn get(self) -> T {
        T::from_little_endian(self.0)
    }

    /// Return the raw little endian value of self
    pub fn raw(self) -> T {
        self.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn be_from_bytes_u32_full() {
        let be = BigEndian::<u32>::from_bytes(&[0x12, 0x34, 0x56, 0x78]);
        assert_eq!(be.get(), 0x1234_5678u32);
    }

    #[test]
    fn be_from_bytes_u32_short() {
        // short slice zero-pads in the MSB positions
        let be = BigEndian::<u32>::from_bytes(&[0x56, 0x78]);
        assert_eq!(be.get(), 0x0000_5678u32);
    }

    #[test]
    fn be_from_bytes_u16() {
        let be = BigEndian::<u16>::from_bytes(&[0xAB, 0xCD]);
        assert_eq!(be.get(), 0xABCDu16);
    }

    #[test]
    fn be_from_bytes_u8() {
        let be = BigEndian::<u8>::from_bytes(&[0x42]);
        assert_eq!(be.get(), 0x42u8);
    }

    #[test]
    fn be_from_bytes_empty() {
        let be = BigEndian::<u32>::from_bytes(&[]);
        assert_eq!(be.get(), 0u32);
    }

    #[test]
    fn le_from_bytes_u32_full() {
        let le = LittleEndian::<u32>::from_bytes(&[0x78, 0x56, 0x34, 0x12]);
        assert_eq!(le.get(), 0x1234_5678u32);
    }

    #[test]
    fn le_from_bytes_u32_short() {
        // short slice zero-pads in the MSB positions
        let le = LittleEndian::<u32>::from_bytes(&[0x78, 0x56]);
        assert_eq!(le.get(), 0x0000_5678u32);
    }

    #[test]
    fn le_from_bytes_u16() {
        let le = LittleEndian::<u16>::from_bytes(&[0xCD, 0xAB]);
        assert_eq!(le.get(), 0xABCDu16);
    }

    #[test]
    fn le_from_bytes_u8() {
        let le = LittleEndian::<u8>::from_bytes(&[0x42]);
        assert_eq!(le.get(), 0x42u8);
    }

    #[test]
    fn le_from_bytes_empty() {
        let le = LittleEndian::<u32>::from_bytes(&[]);
        assert_eq!(le.get(), 0u32);
    }

    #[test]
    fn be_from_bytes_longer_than_type_uses_first_n() {
        // only the first size_of::<u16>() bytes are consumed
        let be = BigEndian::<u16>::from_bytes(&[0x12, 0x34, 0xFF, 0xFF]);
        assert_eq!(be.get(), 0x1234u16);
    }

    #[test]
    fn le_from_bytes_longer_than_type_uses_first_n() {
        let le = LittleEndian::<u16>::from_bytes(&[0x34, 0x12, 0xFF, 0xFF]);
        assert_eq!(le.get(), 0x1234u16);
    }

    #[test]
    fn be_to_bytes_u32() {
        let be = BigEndian::<u32>::new(0x1234_5678u32);
        assert_eq!(be.to_bytes(), [0x12, 0x34, 0x56, 0x78]);
    }

    #[test]
    fn be_to_bytes_u16() {
        let be = BigEndian::<u16>::new(0xABCDu16);
        assert_eq!(be.to_bytes(), [0xAB, 0xCD]);
    }

    #[test]
    fn le_to_bytes_u32() {
        let le = LittleEndian::<u32>::new(0x1234_5678u32);
        assert_eq!(le.to_bytes(), [0x78, 0x56, 0x34, 0x12]);
    }

    #[test]
    fn le_to_bytes_u16() {
        let le = LittleEndian::<u16>::new(0xABCDu16);
        assert_eq!(le.to_bytes(), [0xCD, 0xAB]);
    }

    #[test]
    fn be_roundtrip_to_from_bytes() {
        let orig = BigEndian::<u32>::new(0xDEAD_BEEFu32);
        let bytes = orig.to_bytes();
        let restored = BigEndian::<u32>::from_bytes(bytes.as_ref());
        assert_eq!(orig.get(), restored.get());
    }

    #[test]
    fn le_roundtrip_to_from_bytes() {
        let orig = LittleEndian::<u32>::new(0xDEAD_BEEFu32);
        let bytes = orig.to_bytes();
        let restored = LittleEndian::<u32>::from_bytes(bytes.as_ref());
        assert_eq!(orig.get(), restored.get());
    }

    #[test]
    fn be_ordering_uses_native_value() {
        // 256 > 1 regardless of host byte order
        assert!(BigEndian::<u16>::new(256) > BigEndian::<u16>::new(1));
        assert!(BigEndian::<u16>::new(1) < BigEndian::<u16>::new(256));
        assert_eq!(BigEndian::<u16>::new(42), BigEndian::<u16>::new(42));
    }

    #[test]
    fn le_ordering_uses_native_value() {
        // 256 > 1 regardless of host byte order
        assert!(LittleEndian::<u16>::new(256) > LittleEndian::<u16>::new(1));
        assert!(LittleEndian::<u16>::new(1) < LittleEndian::<u16>::new(256));
        assert_eq!(LittleEndian::<u16>::new(42), LittleEndian::<u16>::new(42));
    }
}
