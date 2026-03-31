//! A generic CRC library supporting 8-bit, 16-bit, and 32-bit CRC computations.
//!
//! This module provides pre-defined CRC variants matching the algorithms available
//! in fwupd's C implementation.
//!
//! # Available CRC variants
//!
//! ## CRC-32
//!
//! | Struct           | Algorithm    |
//! |------------------|--------------|
//! | [`B32Standard`]  | CRC-32       |
//! | [`B32Bzip2`]     | CRC-32/BZIP2 |
//! | [`B32Jamcrc`]    | CRC-32/JAMCRC|
//! | [`B32Mpeg2`]     | CRC-32/MPEG-2|
//! | [`B32Posix`]     | CRC-32/POSIX |
//! | [`B32Sata`]      | CRC-32/SATA  |
//! | [`B32Xfer`]      | CRC-32/XFER  |
//! | [`B32c`]         | CRC-32C      |
//! | [`B32d`]         | CRC-32D      |
//! | [`B32q`]         | CRC-32Q      |
//!
//! ## CRC-16
//!
//! | Struct            | Algorithm       |
//! |-------------------|-----------------|
//! | [`B16Xmodem`]     | CRC-16/XMODEM   |
//! | [`B16Kermit`]     | CRC-16/KERMIT   |
//! | [`B16Usb`]        | CRC-16/USB      |
//! | [`B16Umts`]       | CRC-16/UMTS     |
//! | [`B16Tms37157`]   | CRC-16/TMS37157 |
//! | [`B16Bnr`]        | CRC-16/BNR      |
//!
//! ## CRC-8
//!
//! | Struct             | Algorithm        |
//! |--------------------|------------------|
//! | [`B8Wcdma`]        | CRC-8/WCDMA      |
//! | [`B8Tech3250`]     | CRC-8/TECH-3250  |
//! | [`B8Standard`]     | CRC-8            |
//! | [`B8SaeJ1850`]     | CRC-8/SAE-J1850  |
//! | [`B8Rohc`]         | CRC-8/ROHC       |
//! | [`B8Opensafety`]   | CRC-8/OPENSAFETY |
//! | [`B8Nrsc5`]        | CRC-8/NRSC-5     |
//! | [`B8MifareMad`]    | CRC-8/MIFARE-MAD |
//! | [`B8MaximDow`]     | CRC-8/MAXIM-DOW  |
//! | [`B8Lte`]          | CRC-8/LTE        |
//! | [`B8ICode`]        | CRC-8/I-CODE     |
//! | [`B8Itu`]          | CRC-8/ITU        |
//! | [`B8Hitag`]        | CRC-8/HITAG      |
//! | [`B8GsmB`]         | CRC-8/GSM-B      |
//! | [`B8GsmA`]         | CRC-8/GSM-A      |
//! | [`B8DvbS2`]        | CRC-8/DVB-S2     |
//! | [`B8Darc`]         | CRC-8/DARC       |
//! | [`B8Cdma2000`]     | CRC-8/CDMA2000   |
//! | [`B8Bluetooth`]    | CRC-8/Bluetooth  |
//! | [`B8Autosar`]      | CRC-8/AUTOSAR    |
//!
//! ## Other checksums
//!
//! | Struct         | Algorithm |
//! |----------------|-----------|
//! | [`Misr16`]     | MISR-16   |
//!
//! # One-shot computation
//!
//! Use the [`Crc::crc()`] convenience method to compute a CRC over a single buffer:
//!
//! ```
//! use fwupd::crc::{Crc, B8Wcdma};
//!
//! let crc = B8Wcdma::new();
//! let checksum: u8 = crc.crc(&[0x01, 0x02, 0x03, 0x04]);
//! ```
//!
//! # Incremental computation
//!
//! For streaming or chunked data, use the [`Crc::builder()`] method to obtain a
//! [`CrcBuilder`] that can be fed data in multiple chunks:
//!
//! ```
//! use fwupd::crc::{Crc, B8Wcdma};
//!
//! let crc = B8Wcdma::new();
//! let checksum = crc.builder()
//!     .update(&[0x01, 0x02])
//!     .update(&[0x03, 0x04])
//!     .finalize();
//! ```

use core::ops::{BitAnd, BitXor, Shl};

/// Trait defining the numeric operations required for a CRC width type.
///
/// This trait is implemented for [`u8`], [`u16`], and [`u32`], which correspond to
/// CRC-8, CRC-16, and CRC-32 computations respectively. It provides the bit-width
/// constant and primitive operations used by the generic CRC algorithm.
///
/// This trait is sealed by convention -- users should not need to implement it for
/// other types.
pub trait CrcWidth:
    Copy + BitXor<Output = Self> + BitAnd<Output = Self> + Shl<u32, Output = Self> + PartialEq
{
    /// The number of bits in this CRC type (8, 16, or 32).
    const BITWIDTH: u32;

    /// Returns the zero value for this type.
    fn zero() -> Self;

    /// Returns the one value for this type.
    fn one() -> Self;

    /// Reverses the bits of this value.
    fn reverse_bits(self) -> Self;

    /// Truncating conversion from a `u32` value.
    fn from_u32(val: u32) -> Self;
}

impl CrcWidth for u8 {
    const BITWIDTH: u32 = 8;
    fn zero() -> Self {
        0
    }
    fn one() -> Self {
        1
    }
    fn reverse_bits(self) -> Self {
        u8::reverse_bits(self)
    }
    fn from_u32(val: u32) -> Self {
        val as u8
    }
}

impl CrcWidth for u16 {
    const BITWIDTH: u32 = 16;
    fn zero() -> Self {
        0
    }
    fn one() -> Self {
        1
    }
    fn reverse_bits(self) -> Self {
        u16::reverse_bits(self)
    }
    fn from_u32(val: u32) -> Self {
        val as u16
    }
}

impl CrcWidth for u32 {
    const BITWIDTH: u32 = 32;
    fn zero() -> Self {
        0
    }
    fn one() -> Self {
        1
    }
    fn reverse_bits(self) -> Self {
        u32::reverse_bits(self)
    }
    fn from_u32(val: u32) -> Self {
        val
    }
}

pub(crate) struct CrcParams {
    pub(crate) poly: u32,
    pub(crate) init: u32,
    pub(crate) reflected: bool,
    pub(crate) xorout: u32,
}

/// Accumulate a CRC over a buffer, one byte at a time.
///
/// For CRC-8 (`T::BITWIDTH == 8`), the reflected byte is XORed directly into the
/// accumulator. For CRC-16/32, the byte is shifted to the MSB end of the accumulator
/// before XORing (the shift amount `T::BITWIDTH - 8` is zero for `u8`, making this
/// a unified implementation).
pub(crate) fn crc_step<T: CrcWidth>(c: &CrcParams, buf: &[u8], crc: T) -> T {
    let poly = T::from_u32(c.poly);
    let high_bit = T::one() << (T::BITWIDTH - 1);

    buf.iter().fold(crc, |crc, b| {
        let val: T = T::from_u32(if c.reflected {
            b.reverse_bits() as u32
        } else {
            *b as u32
        });

        // For u8: BITWIDTH - 8 == 0, so this is a no-op shift.
        // For u16/u32: shift the byte up to the MSB of the accumulator.
        let crc = crc ^ (val << (T::BITWIDTH - 8));

        (0..8).fold(crc, |crc, _| {
            if (crc & high_bit) != T::zero() {
                (crc << 1) ^ poly
            } else {
                crc << 1
            }
        })
    })
}

/// Finalize the CRC value by optionally reflecting and XORing with `xorout`.
pub(crate) fn crc_done<T: CrcWidth>(c: &CrcParams, crc: T) -> T {
    let crc = if c.reflected { crc.reverse_bits() } else { crc };
    crc ^ T::from_u32(c.xorout)
}

/// Builder for incremental CRC computation.
///
/// A `CrcBuilder` is obtained from [`Crc::builder()`] and allows feeding data in
/// multiple chunks before producing the final CRC value. This is useful when the
/// data is not available as a single contiguous buffer.
///
/// # Example
///
/// ```
/// use fwupd::crc::{Crc, B32Standard};
///
/// let crc32 = B32Standard::new();
/// let checksum = crc32.builder()
///     .update(&[0x01, 0x02, 0x03])
///     .update(&[0x04, 0x05, 0x06])
///     .update(&[0x07, 0x08, 0x09])
///     .finalize();
/// assert_eq!(checksum, 0x40EFAB9E);
/// ```
pub struct CrcBuilder<'a, T: CrcWidth> {
    params: &'a CrcParams,
    crc: T,
}

impl<'a, T: CrcWidth> CrcBuilder<'a, T> {
    /// Feed a chunk of data into the CRC computation.
    ///
    /// This method can be called multiple times to process data incrementally.
    /// When all data has been added, call [`finalize()`](CrcBuilder::finalize)
    /// to apply the final reflection and XOR-out steps and return the CRC value.
    ///
    /// # Example
    ///
    /// ```
    /// use fwupd::crc::{Crc, B16Usb};
    ///
    /// let crc16 = B16Usb::new();
    /// let checksum = crc16.builder()
    ///     .update(&[0x01, 0x02, 0x03, 0x04, 0x05])
    ///     .update(&[0x06, 0x07, 0x08, 0x09])
    ///     .finalize();
    /// assert_eq!(checksum, 0x4DF1u16);
    /// ```
    pub fn update(mut self, buf: &[u8]) -> Self {
        self.crc = crc_step(self.params, buf, self.crc);
        self
    }

    /// Finalize the CRC computation and return the result.
    pub fn finalize(self) -> T {
        crc_done(self.params, self.crc)
    }
}

/// Trait for types that can compute a CRC of width `T`.
///
/// Each CRC variant struct (e.g. [`B32Standard`], [`B16Usb`]) implements this trait
/// for the appropriate width type. The trait provides two ways to compute a CRC:
///
/// - [`crc()`](Crc::crc) -- convenience method for computing a CRC over a single buffer
/// - [`builder()`](Crc::builder) -- returns a [`CrcBuilder`] for incremental computation
///
/// # Example
///
/// ```
/// use fwupd::crc::{Crc, B8Standard};
///
/// let crc8 = B8Standard::new();
///
/// // One-shot computation:
/// let checksum = crc8.crc(&[0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09]);
/// assert_eq!(checksum, 0x85u8);
/// ```
pub trait Crc<T: CrcWidth> {
    /// Create a [`CrcBuilder`] for incremental CRC computation.
    fn builder(&self) -> CrcBuilder<'_, T>;

    /// Compute the CRC over a single contiguous buffer.
    ///
    /// This is a convenience method equivalent to:
    /// ```ignore
    /// self.builder().update(buf).finalize()
    /// ```
    fn crc(&self, buf: &[u8]) -> T {
        self.builder().update(buf).finalize()
    }
}

macro_rules! declare_crc {
    ($(#[$meta:meta])* $name:ident, $poly:expr, $init:expr, $reflected:expr, $xorout:expr) => {
        $(#[$meta])*
        pub struct $name {
            params: CrcParams,
        }
        impl Default for $name {
            fn default() -> Self {
                Self::new()
            }
        }
        impl $name {
            /// Create a new instance of this CRC variant.
            pub fn new() -> Self {
                Self {
                    params: CrcParams {
                        poly: $poly,
                        init: $init,
                        reflected: $reflected,
                        xorout: $xorout,
                    },
                }
            }
        }
    };
}

macro_rules! impl_crc {
    ($name:ty, $t:ty) => {
        impl Crc<$t> for $name {
            fn builder(&self) -> CrcBuilder<'_, $t> {
                CrcBuilder {
                    params: &self.params,
                    crc: <$t>::from_u32(self.params.init),
                }
            }
        }
    };
}

// CRC-32 variants

declare_crc!(
    /// CRC-32 (standard, ISO 3309).
    B32Standard, 0x04C11DB7, 0xFFFFFFFF, true, 0xFFFFFFFF
);
impl_crc!(B32Standard, u32);

declare_crc!(
    /// CRC-32/BZIP2.
    B32Bzip2, 0x04C11DB7, 0xFFFFFFFF, false, 0xFFFFFFFF
);
impl_crc!(B32Bzip2, u32);

declare_crc!(
    /// CRC-32/JAMCRC.
    B32Jamcrc, 0x04C11DB7, 0xFFFFFFFF, true, 0x00000000
);
impl_crc!(B32Jamcrc, u32);

declare_crc!(
    /// CRC-32/MPEG-2.
    B32Mpeg2, 0x04C11DB7, 0xFFFFFFFF, false, 0x00000000
);
impl_crc!(B32Mpeg2, u32);

declare_crc!(
    /// CRC-32/POSIX.
    B32Posix, 0x04C11DB7, 0x00000000, false, 0xFFFFFFFF
);
impl_crc!(B32Posix, u32);

declare_crc!(
    /// CRC-32/SATA.
    B32Sata, 0x04C11DB7, 0x52325032, false, 0x00000000
);
impl_crc!(B32Sata, u32);

declare_crc!(
    /// CRC-32/XFER.
    B32Xfer, 0x000000AF, 0x00000000, false, 0x00000000
);
impl_crc!(B32Xfer, u32);

declare_crc!(
    /// CRC-32C (Castagnoli).
    B32c, 0x1EDC6F41, 0xFFFFFFFF, true, 0xFFFFFFFF
);
impl_crc!(B32c, u32);

declare_crc!(
    /// CRC-32D.
    B32d, 0xA833982B, 0xFFFFFFFF, true, 0xFFFFFFFF
);
impl_crc!(B32d, u32);

declare_crc!(
    /// CRC-32Q.
    B32q, 0x814141AB, 0x00000000, false, 0x00000000
);
impl_crc!(B32q, u32);

// CRC-16 variants

declare_crc!(
    /// CRC-16/XMODEM.
    B16Xmodem, 0x1021, 0x0000, false, 0x0000
);
impl_crc!(B16Xmodem, u16);

declare_crc!(
    /// CRC-16/KERMIT.
    B16Kermit, 0x1021, 0x0000, true, 0x0000
);
impl_crc!(B16Kermit, u16);

declare_crc!(
    /// CRC-16/USB.
    B16Usb, 0x8005, 0xFFFF, true, 0xFFFF
);
impl_crc!(B16Usb, u16);

declare_crc!(
    /// CRC-16/UMTS.
    B16Umts, 0x8005, 0x0000, false, 0x0000
);
impl_crc!(B16Umts, u16);

declare_crc!(
    /// CRC-16/TMS37157.
    B16Tms37157, 0x1021, 0x89EC, true, 0x0000
);
impl_crc!(B16Tms37157, u16);

declare_crc!(
    /// CRC-16/BNR.
    B16Bnr, 0x8005, 0xFFFF, false, 0x0000
);
impl_crc!(B16Bnr, u16);

// CRC-8 variants

declare_crc!(
    /// CRC-8/WCDMA.
    B8Wcdma, 0x9B, 0x00, true, 0x00
);
impl_crc!(B8Wcdma, u8);

declare_crc!(
    /// CRC-8/TECH-3250.
    B8Tech3250, 0x1D, 0xFF, true, 0x00
);
impl_crc!(B8Tech3250, u8);

declare_crc!(
    /// CRC-8 (standard).
    B8Standard, 0x07, 0x00, false, 0x00
);
impl_crc!(B8Standard, u8);

declare_crc!(
    /// CRC-8/SAE-J1850.
    B8SaeJ1850, 0x1D, 0xFF, false, 0xFF
);
impl_crc!(B8SaeJ1850, u8);

declare_crc!(
    /// CRC-8/ROHC.
    B8Rohc, 0x07, 0xFF, true, 0x00
);
impl_crc!(B8Rohc, u8);

declare_crc!(
    /// CRC-8/OPENSAFETY.
    B8Opensafety, 0x2F, 0x00, false, 0x00
);
impl_crc!(B8Opensafety, u8);

declare_crc!(
    /// CRC-8/NRSC-5.
    B8Nrsc5, 0x31, 0xFF, false, 0x00
);
impl_crc!(B8Nrsc5, u8);

declare_crc!(
    /// CRC-8/MIFARE-MAD.
    B8MifareMad, 0x1D, 0xC7, false, 0x00
);
impl_crc!(B8MifareMad, u8);

declare_crc!(
    /// CRC-8/MAXIM-DOW.
    B8MaximDow, 0x31, 0x00, true, 0x00
);
impl_crc!(B8MaximDow, u8);

declare_crc!(
    /// CRC-8/LTE.
    B8Lte, 0x9B, 0x00, false, 0x00
);
impl_crc!(B8Lte, u8);

declare_crc!(
    /// CRC-8/I-CODE.
    B8ICode, 0x1D, 0xFD, false, 0x00
);
impl_crc!(B8ICode, u8);

declare_crc!(
    /// CRC-8/ITU.
    B8Itu, 0x07, 0x00, false, 0x55
);
impl_crc!(B8Itu, u8);

declare_crc!(
    /// CRC-8/HITAG.
    B8Hitag, 0x1D, 0xFF, false, 0x00
);
impl_crc!(B8Hitag, u8);

declare_crc!(
    /// CRC-8/GSM-B.
    B8GsmB, 0x49, 0x00, false, 0xFF
);
impl_crc!(B8GsmB, u8);

declare_crc!(
    /// CRC-8/GSM-A.
    B8GsmA, 0x1D, 0x00, false, 0x00
);
impl_crc!(B8GsmA, u8);

declare_crc!(
    /// CRC-8/DVB-S2.
    B8DvbS2, 0xD5, 0x00, false, 0x00
);
impl_crc!(B8DvbS2, u8);

declare_crc!(
    /// CRC-8/DARC.
    B8Darc, 0x39, 0x00, true, 0x00
);
impl_crc!(B8Darc, u8);

declare_crc!(
    /// CRC-8/CDMA2000.
    B8Cdma2000, 0x9B, 0xFF, false, 0x00
);
impl_crc!(B8Cdma2000, u8);

declare_crc!(
    /// CRC-8/Bluetooth.
    B8Bluetooth, 0xA7, 0x00, true, 0x00
);
impl_crc!(B8Bluetooth, u8);

declare_crc!(
    /// CRC-8/AUTOSAR.
    B8Autosar, 0x2F, 0xFF, false, 0xFF
);
impl_crc!(B8Autosar, u8);

// -- MISR-16 --

/// Compute one step of the MISR-16 checksum.
fn misr16_word_step(cur: u16, new: u16) -> u16 {
    let mut bit0 = cur ^ (new & 1);
    bit0 ^= cur >> 1;
    bit0 ^= cur >> 2;
    bit0 ^= cur >> 4;
    bit0 ^= cur >> 5;
    bit0 ^= cur >> 7;
    bit0 ^= cur >> 11;
    bit0 ^= cur >> 15;
    let res = (cur << 1) ^ new;
    (res & !1) | (bit0 & 1)
}

/// Accumulate MISR-16 over a byte buffer interpreted as little-endian 16-bit
/// words. Any trailing odd byte is ignored.
fn misr16_step(buf: &[u8], crc: u16) -> u16 {
    buf.chunks_exact(2).fold(crc, |acc, chunk| {
        let word = u16::from_le_bytes([chunk[0], chunk[1]]);
        misr16_word_step(acc, word)
    })
}

/// MISR-16 (Multiple Input Signature Register) checksum.
///
/// Unlike the standard CRC variants, MISR-16 uses a different algorithm based
/// on a linear feedback shift register. The input buffer is interpreted as a
/// sequence of little-endian 16-bit words.
///
/// # Example
///
/// ```
/// use fwupd::crc::Misr16;
///
/// let buf = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
/// assert_eq!(Misr16::new(0x0000).crc(&buf).unwrap(), 0x040D);
/// assert_eq!(Misr16::new(0xFFFF).crc(&buf).unwrap(), 0xFBFA);
/// ```
pub struct Misr16 {
    init: u16,
}

impl Misr16 {
    /// Create a new MISR-16 instance with the given initial value,
    /// typically `0x0000`.
    pub fn new(init: u16) -> Self {
        Self { init }
    }

    /// Compute the MISR-16 checksum over a byte buffer.
    ///
    /// The buffer is interpreted as a sequence of little-endian 16-bit words.
    /// Returns an error if the buffer has an odd number of bytes.
    pub fn crc(&self, buf: &[u8]) -> Result<u16, &'static str> {
        if !buf.len().is_multiple_of(2) {
            return Err("buffer length must be even");
        }
        Ok(misr16_step(buf, self.init))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_BUF: [u8; 9] = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09];

    // CRC-8: verified against C fu_crc8() test
    #[test]
    fn crc8_standard() {
        assert_eq!(B8Standard::new().crc(&TEST_BUF), 0x85u8);
    }

    // CRC-16: verified against C fu_crc16() test
    #[test]
    fn crc16_usb() {
        assert_eq!(B16Usb::new().crc(&TEST_BUF), 0x4DF1u16);
    }

    // CRC-32: all variants verified against C fu_crc32() tests
    // (values from https://crccalc.com/?method=CRC-32)
    #[test]
    fn crc32_standard() {
        assert_eq!(B32Standard::new().crc(&TEST_BUF), 0x40EFAB9E);
    }

    #[test]
    fn crc32_bzip2() {
        assert_eq!(B32Bzip2::new().crc(&TEST_BUF), 0x89AE7A5C);
    }

    #[test]
    fn crc32_jamcrc() {
        assert_eq!(B32Jamcrc::new().crc(&TEST_BUF), 0xBF105461);
    }

    #[test]
    fn crc32_mpeg2() {
        assert_eq!(B32Mpeg2::new().crc(&TEST_BUF), 0x765185A3);
    }

    #[test]
    fn crc32_posix() {
        assert_eq!(B32Posix::new().crc(&TEST_BUF), 0x037915C4);
    }

    #[test]
    fn crc32_sata() {
        assert_eq!(B32Sata::new().crc(&TEST_BUF), 0xBA55CCAC);
    }

    #[test]
    fn crc32_xfer() {
        assert_eq!(B32Xfer::new().crc(&TEST_BUF), 0x868E70FC);
    }

    #[test]
    fn crc32c() {
        assert_eq!(B32c::new().crc(&TEST_BUF), 0x5A14B9F9);
    }

    #[test]
    fn crc32d() {
        assert_eq!(B32d::new().crc(&TEST_BUF), 0x68AD8D3C);
    }

    #[test]
    fn crc32q() {
        assert_eq!(B32q::new().crc(&TEST_BUF), 0xE955C875);
    }

    #[test]
    fn crc32_builder_chunked() {
        let crc32 = B32Standard::new();
        let result = crc32
            .builder()
            .update(&TEST_BUF[..4])
            .update(&TEST_BUF[4..])
            .finalize();
        assert_eq!(result, 0x40EFAB9E);
    }

    // MISR-16: verified against C fu_crc_misr16() tests
    // The C test uses buf[0..8] (even-length prefix of TEST_BUF)
    #[test]
    fn misr16_zero_init() {
        assert_eq!(Misr16::new(0x0000).crc(&TEST_BUF[..8]), Ok(0x040D));
    }

    #[test]
    fn misr16_ffff_init() {
        assert_eq!(Misr16::new(0xFFFF).crc(&TEST_BUF[..8]), Ok(0xFBFA));
    }

    #[test]
    fn misr16_odd_length() {
        assert!(Misr16::new(0).crc(&TEST_BUF).is_err());
    }
}
