/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! A unified CRC library supporting 8-bit, 16-bit, and 32-bit CRC computations
//! plus MISR-16, all accessible through runtime-dispatched types.
//!
//! # Overview
//!
//! Instead of one struct per CRC algorithm, this module provides:
//!
//! - [`CrcKind`] -- an enum listing every supported algorithm
//! - [`Crc`] -- high-level engine for one-shot and incremental computation
//! - [`RawCrc`] -- low-level engine exposing raw intermediate values for FFI
//! - [`CrcValue`] -- the result type, with `width()` and typed accessors
//! - [`crc32_fast()`] -- hardware-accelerated CRC-32 with finalized-in/out semantics
//!
//! [`Crc`] and [`RawCrc`] are both constructed from a [`CrcKind`] and compute
//! identical results. They differ in their streaming interface:
//!
//! - [`Crc`] manages state internally via [`CrcState`], suitable for Rust callers
//! - [`RawCrc`] carries state as a plain `u32`, suitable for C FFI boundaries
//!
//! # One-shot computation
//!
//! ```
//! use fwupd::crc::{Crc, CrcKind};
//!
//! let crc = Crc::new(CrcKind::B8Wcdma);
//! let checksum = crc.crc(&[0x01, 0x02, 0x03, 0x04]).unwrap();
//! assert_eq!(checksum.as_u8(), Some(0xD6));
//! ```
//!
//! # Incremental computation
//!
//! For streaming or chunked data, call [`Crc::update()`] to start an
//! incremental computation. This returns a [`CrcState`] that accumulates
//! data and must be finalized:
//!
//! ```
//! use fwupd::crc::{Crc, CrcKind};
//!
//! let crc = Crc::new(CrcKind::B32Standard);
//! let checksum = crc
//!     .update(&[0x01, 0x02, 0x03])
//!     .update(&[0x04, 0x05, 0x06])
//!     .update(&[0x07, 0x08, 0x09])
//!     .finalize()
//!     .unwrap();
//! assert_eq!(checksum.as_u32(), Some(0x40EFAB9E));
//! ```
//!
//! # Raw streaming (for FFI)
//!
//! [`RawCrc`] exposes the CRC accumulator as a plain `u32` for use across
//! FFI boundaries where an opaque state object cannot be passed:
//!
//! ```
//! use fwupd::crc::{RawCrc, CrcKind};
//!
//! let engine = RawCrc::new(CrcKind::B32Standard);
//! let mut crc = engine.init();
//! crc = engine.step(&[0x01, 0x02, 0x03], crc);
//! crc = engine.step(&[0x04, 0x05, 0x06], crc);
//! crc = engine.step(&[0x07, 0x08, 0x09], crc);
//! let result = engine.done(crc);
//! assert_eq!(result.as_u32(), Some(0x40EFAB9E));
//! ```
//!
//! # Hardware-accelerated CRC-32
//!
//! [`CrcKind::B32Standard`] uses the [`crc32fast`](https://docs.rs/crc32fast)
//! crate for hardware-accelerated CRC-32 computation (PCLMULQDQ on x86_64,
//! NEON on aarch64, table-based fallback elsewhere). This is transparent --
//! callers use the same API as all other variants.

use core::fmt;
use core::ops::{BitAnd, BitXor, Shl};

// ---------------------------------------------------------------------------
// Internal: CrcWidth trait and implementations
// ---------------------------------------------------------------------------

/// Trait defining the numeric operations required for a CRC width type.
///
/// Implemented for `u8`, `u16`, and `u32`.
trait CrcWidth:
    Copy + BitXor<Output = Self> + BitAnd<Output = Self> + Shl<u32, Output = Self> + PartialEq
{
    const BITWIDTH: u32;
    fn zero() -> Self;
    fn one() -> Self;
    fn reverse_bits(self) -> Self;
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

// ---------------------------------------------------------------------------
// Internal: CRC parameters and generic computation
// ---------------------------------------------------------------------------

#[derive(Clone, Copy)]
struct CrcParams {
    poly: u32,
    init: u32,
    reflected: bool,
    xorout: u32,
}

/// Accumulate a CRC over a buffer, one byte at a time.
fn raw_step<T: CrcWidth>(c: &CrcParams, buf: &[u8], crc: T) -> T {
    let poly = T::from_u32(c.poly);
    let high_bit = T::one() << (T::BITWIDTH - 1);

    buf.iter().fold(crc, |crc, b| {
        let val: T = T::from_u32(if c.reflected {
            b.reverse_bits() as u32
        } else {
            *b as u32
        });

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
fn raw_done<T: CrcWidth>(c: &CrcParams, crc: T) -> T {
    let crc = if c.reflected { crc.reverse_bits() } else { crc };
    crc ^ T::from_u32(c.xorout)
}

// ---------------------------------------------------------------------------
// Internal: MISR-16
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Public: CrcKind
// ---------------------------------------------------------------------------

/// Every supported CRC algorithm.
///
/// Use this enum to select an algorithm at runtime, then pass it to
/// [`Crc::new()`] to obtain a [`Crc`] instance.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CrcKind {
    // CRC-32
    /// CRC-32 (standard, ISO 3309). Hardware-accelerated via `crc32fast`.
    B32Standard,
    /// CRC-32/BZIP2.
    B32Bzip2,
    /// CRC-32/JAMCRC.
    B32Jamcrc,
    /// CRC-32/MPEG-2.
    B32Mpeg2,
    /// CRC-32/POSIX.
    B32Posix,
    /// CRC-32/SATA.
    B32Sata,
    /// CRC-32/XFER.
    B32Xfer,
    /// CRC-32C (Castagnoli).
    B32c,
    /// CRC-32D.
    B32d,
    /// CRC-32Q.
    B32q,

    // CRC-16
    /// CRC-16/XMODEM.
    B16Xmodem,
    /// CRC-16/KERMIT.
    B16Kermit,
    /// CRC-16/USB.
    B16Usb,
    /// CRC-16/UMTS.
    B16Umts,
    /// CRC-16/TMS37157.
    B16Tms37157,
    /// CRC-16/BNR.
    B16Bnr,

    // CRC-8
    /// CRC-8/WCDMA.
    B8Wcdma,
    /// CRC-8/TECH-3250.
    B8Tech3250,
    /// CRC-8 (standard).
    B8Standard,
    /// CRC-8/SAE-J1850.
    B8SaeJ1850,
    /// CRC-8/ROHC.
    B8Rohc,
    /// CRC-8/OPENSAFETY.
    B8Opensafety,
    /// CRC-8/NRSC-5.
    B8Nrsc5,
    /// CRC-8/MIFARE-MAD.
    B8MifareMad,
    /// CRC-8/MAXIM-DOW.
    B8MaximDow,
    /// CRC-8/LTE.
    B8Lte,
    /// CRC-8/I-CODE.
    B8ICode,
    /// CRC-8/ITU.
    B8Itu,
    /// CRC-8/HITAG.
    B8Hitag,
    /// CRC-8/GSM-B.
    B8GsmB,
    /// CRC-8/GSM-A.
    B8GsmA,
    /// CRC-8/DVB-S2.
    B8DvbS2,
    /// CRC-8/DARC.
    B8Darc,
    /// CRC-8/CDMA2000.
    B8Cdma2000,
    /// CRC-8/Bluetooth.
    B8Bluetooth,
    /// CRC-8/AUTOSAR.
    B8Autosar,

    // MISR-16
    /// MISR-16 (Multiple Input Signature Register) with a given initial value.
    ///
    /// Common init values are `0x0000` and `0xFFFF`.
    Misr16 {
        /// Initial accumulator value.
        init: u16,
    },
}

/// The CRC bit-width for a given variant.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Width {
    W8,
    W16,
    W32,
}

impl CrcKind {
    /// All CRC algorithm variants (excludes [`Misr16`](CrcKind::Misr16) which
    /// requires an `init` parameter).
    ///
    /// Useful for iterating over all known algorithms, e.g. to find which
    /// algorithm produced a given checksum.
    pub const ALL: &[CrcKind] = &[
        CrcKind::B32Standard,
        CrcKind::B32Bzip2,
        CrcKind::B32Jamcrc,
        CrcKind::B32Mpeg2,
        CrcKind::B32Posix,
        CrcKind::B32Sata,
        CrcKind::B32Xfer,
        CrcKind::B32c,
        CrcKind::B32d,
        CrcKind::B32q,
        CrcKind::B16Xmodem,
        CrcKind::B16Kermit,
        CrcKind::B16Usb,
        CrcKind::B16Umts,
        CrcKind::B16Tms37157,
        CrcKind::B16Bnr,
        CrcKind::B8Wcdma,
        CrcKind::B8Tech3250,
        CrcKind::B8Standard,
        CrcKind::B8SaeJ1850,
        CrcKind::B8Rohc,
        CrcKind::B8Opensafety,
        CrcKind::B8Nrsc5,
        CrcKind::B8MifareMad,
        CrcKind::B8MaximDow,
        CrcKind::B8Lte,
        CrcKind::B8ICode,
        CrcKind::B8Itu,
        CrcKind::B8Hitag,
        CrcKind::B8GsmB,
        CrcKind::B8GsmA,
        CrcKind::B8DvbS2,
        CrcKind::B8Darc,
        CrcKind::B8Cdma2000,
        CrcKind::B8Bluetooth,
        CrcKind::B8Autosar,
    ];

    /// Return the CRC parameters and width for this variant.
    ///
    /// For MISR-16 this returns `None` for params (different algorithm).
    fn params_and_width(self) -> (Option<CrcParams>, Width) {
        use CrcKind::*;
        match self {
            // CRC-32
            B32Standard => (
                Some(CrcParams {
                    poly: 0x04C11DB7,
                    init: 0xFFFFFFFF,
                    reflected: true,
                    xorout: 0xFFFFFFFF,
                }),
                Width::W32,
            ),
            B32Bzip2 => (
                Some(CrcParams {
                    poly: 0x04C11DB7,
                    init: 0xFFFFFFFF,
                    reflected: false,
                    xorout: 0xFFFFFFFF,
                }),
                Width::W32,
            ),
            B32Jamcrc => (
                Some(CrcParams {
                    poly: 0x04C11DB7,
                    init: 0xFFFFFFFF,
                    reflected: true,
                    xorout: 0x00000000,
                }),
                Width::W32,
            ),
            B32Mpeg2 => (
                Some(CrcParams {
                    poly: 0x04C11DB7,
                    init: 0xFFFFFFFF,
                    reflected: false,
                    xorout: 0x00000000,
                }),
                Width::W32,
            ),
            B32Posix => (
                Some(CrcParams {
                    poly: 0x04C11DB7,
                    init: 0x00000000,
                    reflected: false,
                    xorout: 0xFFFFFFFF,
                }),
                Width::W32,
            ),
            B32Sata => (
                Some(CrcParams {
                    poly: 0x04C11DB7,
                    init: 0x52325032,
                    reflected: false,
                    xorout: 0x00000000,
                }),
                Width::W32,
            ),
            B32Xfer => (
                Some(CrcParams {
                    poly: 0x000000AF,
                    init: 0x00000000,
                    reflected: false,
                    xorout: 0x00000000,
                }),
                Width::W32,
            ),
            B32c => (
                Some(CrcParams {
                    poly: 0x1EDC6F41,
                    init: 0xFFFFFFFF,
                    reflected: true,
                    xorout: 0xFFFFFFFF,
                }),
                Width::W32,
            ),
            B32d => (
                Some(CrcParams {
                    poly: 0xA833982B,
                    init: 0xFFFFFFFF,
                    reflected: true,
                    xorout: 0xFFFFFFFF,
                }),
                Width::W32,
            ),
            B32q => (
                Some(CrcParams {
                    poly: 0x814141AB,
                    init: 0x00000000,
                    reflected: false,
                    xorout: 0x00000000,
                }),
                Width::W32,
            ),

            // CRC-16
            B16Xmodem => (
                Some(CrcParams {
                    poly: 0x1021,
                    init: 0x0000,
                    reflected: false,
                    xorout: 0x0000,
                }),
                Width::W16,
            ),
            B16Kermit => (
                Some(CrcParams {
                    poly: 0x1021,
                    init: 0x0000,
                    reflected: true,
                    xorout: 0x0000,
                }),
                Width::W16,
            ),
            B16Usb => (
                Some(CrcParams {
                    poly: 0x8005,
                    init: 0xFFFF,
                    reflected: true,
                    xorout: 0xFFFF,
                }),
                Width::W16,
            ),
            B16Umts => (
                Some(CrcParams {
                    poly: 0x8005,
                    init: 0x0000,
                    reflected: false,
                    xorout: 0x0000,
                }),
                Width::W16,
            ),
            B16Tms37157 => (
                Some(CrcParams {
                    poly: 0x1021,
                    init: 0x89EC,
                    reflected: true,
                    xorout: 0x0000,
                }),
                Width::W16,
            ),
            B16Bnr => (
                Some(CrcParams {
                    poly: 0x8005,
                    init: 0xFFFF,
                    reflected: false,
                    xorout: 0x0000,
                }),
                Width::W16,
            ),

            // CRC-8
            B8Wcdma => (
                Some(CrcParams {
                    poly: 0x9B,
                    init: 0x00,
                    reflected: true,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Tech3250 => (
                Some(CrcParams {
                    poly: 0x1D,
                    init: 0xFF,
                    reflected: true,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Standard => (
                Some(CrcParams {
                    poly: 0x07,
                    init: 0x00,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8SaeJ1850 => (
                Some(CrcParams {
                    poly: 0x1D,
                    init: 0xFF,
                    reflected: false,
                    xorout: 0xFF,
                }),
                Width::W8,
            ),
            B8Rohc => (
                Some(CrcParams {
                    poly: 0x07,
                    init: 0xFF,
                    reflected: true,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Opensafety => (
                Some(CrcParams {
                    poly: 0x2F,
                    init: 0x00,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Nrsc5 => (
                Some(CrcParams {
                    poly: 0x31,
                    init: 0xFF,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8MifareMad => (
                Some(CrcParams {
                    poly: 0x1D,
                    init: 0xC7,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8MaximDow => (
                Some(CrcParams {
                    poly: 0x31,
                    init: 0x00,
                    reflected: true,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Lte => (
                Some(CrcParams {
                    poly: 0x9B,
                    init: 0x00,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8ICode => (
                Some(CrcParams {
                    poly: 0x1D,
                    init: 0xFD,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Itu => (
                Some(CrcParams {
                    poly: 0x07,
                    init: 0x00,
                    reflected: false,
                    xorout: 0x55,
                }),
                Width::W8,
            ),
            B8Hitag => (
                Some(CrcParams {
                    poly: 0x1D,
                    init: 0xFF,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8GsmB => (
                Some(CrcParams {
                    poly: 0x49,
                    init: 0x00,
                    reflected: false,
                    xorout: 0xFF,
                }),
                Width::W8,
            ),
            B8GsmA => (
                Some(CrcParams {
                    poly: 0x1D,
                    init: 0x00,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8DvbS2 => (
                Some(CrcParams {
                    poly: 0xD5,
                    init: 0x00,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Darc => (
                Some(CrcParams {
                    poly: 0x39,
                    init: 0x00,
                    reflected: true,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Cdma2000 => (
                Some(CrcParams {
                    poly: 0x9B,
                    init: 0xFF,
                    reflected: false,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Bluetooth => (
                Some(CrcParams {
                    poly: 0xA7,
                    init: 0x00,
                    reflected: true,
                    xorout: 0x00,
                }),
                Width::W8,
            ),
            B8Autosar => (
                Some(CrcParams {
                    poly: 0x2F,
                    init: 0xFF,
                    reflected: false,
                    xorout: 0xFF,
                }),
                Width::W8,
            ),

            // MISR-16 uses a different algorithm entirely
            Misr16 { .. } => (None, Width::W16),
        }
    }
}

// ---------------------------------------------------------------------------
// Public: CrcValue
// ---------------------------------------------------------------------------

/// The result of a CRC computation.
///
/// Use [`width()`](CrcValue::width) to determine the CRC bit-width, then the
/// corresponding accessor ([`as_u8()`](CrcValue::as_u8),
/// [`as_u16()`](CrcValue::as_u16), [`as_u32()`](CrcValue::as_u32)) to extract
/// the value.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CrcValue {
    /// An 8-bit CRC result.
    U8(u8),
    /// A 16-bit CRC or MISR-16 result.
    U16(u16),
    /// A 32-bit CRC result.
    U32(u32),
}

impl CrcValue {
    /// The bit-width of this CRC result (8, 16, or 32).
    pub fn width(&self) -> u32 {
        match self {
            CrcValue::U8(_) => 8,
            CrcValue::U16(_) => 16,
            CrcValue::U32(_) => 32,
        }
    }

    /// Extract the value as `u8`, returning `Some` only for 8-bit CRC results.
    pub fn as_u8(&self) -> Option<u8> {
        match self {
            CrcValue::U8(v) => Some(*v),
            _ => None,
        }
    }

    /// Extract the value as `u16`, returning `Some` only for 16-bit results.
    pub fn as_u16(&self) -> Option<u16> {
        match self {
            CrcValue::U16(v) => Some(*v),
            _ => None,
        }
    }

    /// Extract the value as `u32`, returning `Some` only for 32-bit CRC results.
    pub fn as_u32(&self) -> Option<u32> {
        match self {
            CrcValue::U32(v) => Some(*v),
            _ => None,
        }
    }
}

// ---------------------------------------------------------------------------
// Public: CrcError
// ---------------------------------------------------------------------------

/// Errors that can occur during CRC computation.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum CrcError {
    /// The input buffer has an odd number of bytes, which is invalid for MISR-16
    /// (it interprets input as little-endian 16-bit words).
    OddLength,
}

impl fmt::Display for CrcError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CrcError::OddLength => write!(f, "buffer length must be even (required by MISR-16)"),
        }
    }
}

impl std::error::Error for CrcError {}

// ---------------------------------------------------------------------------
// Public: Crc
// ---------------------------------------------------------------------------

/// A runtime-dispatched CRC computation engine.
///
/// Constructed from a [`CrcKind`] variant, this struct provides one-shot and
/// incremental CRC computation over byte buffers.
///
/// For low-level streaming with raw intermediate values, see [`RawCrc`].
///
/// # Example
///
/// ```
/// use fwupd::crc::{Crc, CrcKind};
///
/// let crc = Crc::new(CrcKind::B32Standard);
/// let result = crc.crc(&[0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09]).unwrap();
/// assert_eq!(result.as_u32(), Some(0x40EFAB9E));
/// ```
#[derive(Clone, Copy)]
pub struct Crc {
    kind: CrcKind,
    params: Option<CrcParams>,
    width: Width,
}

impl Crc {
    /// Create a new CRC engine for the given algorithm.
    pub fn new(kind: CrcKind) -> Self {
        let (params, width) = kind.params_and_width();
        Self {
            kind,
            params,
            width,
        }
    }

    /// Return the algorithm variant this engine was constructed with.
    pub fn kind(&self) -> CrcKind {
        self.kind
    }

    /// Return the bit-width of the CRC result (8, 16, or 32).
    ///
    /// This matches the [`CrcValue::width()`] of values produced by this engine.
    pub fn width(&self) -> u32 {
        match self.width {
            Width::W8 => 8,
            Width::W16 => 16,
            Width::W32 => 32,
        }
    }

    /// Compute the CRC over a single contiguous buffer.
    ///
    /// Returns `Err(CrcError::OddLength)` if this is a MISR-16 instance and the
    /// buffer has an odd number of bytes.
    pub fn crc(&self, buf: &[u8]) -> Result<CrcValue, CrcError> {
        match self.kind {
            CrcKind::B32Standard => Ok(CrcValue::U32(crc32fast::hash(buf))),
            CrcKind::Misr16 { init } => {
                if !buf.len().is_multiple_of(2) {
                    return Err(CrcError::OddLength);
                }
                Ok(CrcValue::U16(misr16_step(buf, init)))
            }
            _ => {
                let params = self.params.unwrap();
                match self.width {
                    Width::W32 => {
                        let crc = raw_step::<u32>(&params, buf, params.init);
                        Ok(CrcValue::U32(raw_done(&params, crc)))
                    }
                    Width::W16 => {
                        let crc = raw_step::<u16>(&params, buf, params.init as u16);
                        Ok(CrcValue::U16(raw_done(&params, crc)))
                    }
                    Width::W8 => {
                        let crc = raw_step::<u8>(&params, buf, params.init as u8);
                        Ok(CrcValue::U8(raw_done(&params, crc)))
                    }
                }
            }
        }
    }

    /// Start an incremental CRC computation by feeding the first chunk of data.
    ///
    /// Returns a [`CrcState`] that can be fed additional chunks via
    /// [`CrcState::update()`] and finalized with [`CrcState::finalize()`].
    ///
    /// # Example
    ///
    /// ```
    /// use fwupd::crc::{Crc, CrcKind};
    ///
    /// let crc = Crc::new(CrcKind::B32Standard);
    /// let result = crc
    ///     .update(&[0x01, 0x02, 0x03])
    ///     .update(&[0x04, 0x05, 0x06])
    ///     .update(&[0x07, 0x08, 0x09])
    ///     .finalize()
    ///     .unwrap();
    /// assert_eq!(result.as_u32(), Some(0x40EFAB9E));
    /// ```
    pub fn update(&self, buf: &[u8]) -> CrcState {
        let mut state = match self.kind {
            CrcKind::B32Standard => CrcState {
                params: self.params,
                inner: StateInner::Fast(crc32fast::Hasher::new()),
            },
            CrcKind::Misr16 { init } => CrcState {
                params: None,
                inner: StateInner::Misr {
                    acc: init,
                    leftover: None,
                },
            },
            _ => {
                let params = self.params.unwrap();
                let inner = match self.width {
                    Width::W32 => StateInner::Generic32(params.init),
                    Width::W16 => StateInner::Generic16(params.init as u16),
                    Width::W8 => StateInner::Generic8(params.init as u8),
                };
                CrcState {
                    params: self.params,
                    inner,
                }
            }
        };
        state.feed(buf);
        state
    }
}

// ---------------------------------------------------------------------------
// Public: RawCrc
// ---------------------------------------------------------------------------

/// Low-level CRC streaming engine using raw intermediate values.
///
/// Unlike [`Crc`] which manages state internally, `RawCrc` exposes the raw
/// CRC accumulator as a plain `u32` that callers pass between calls. This is
/// useful for FFI boundaries where an opaque state object cannot be passed.
///
/// Use [`init()`](RawCrc::init) to get the starting value, then call
/// [`step()`](RawCrc::step) for each chunk, and [`done()`](RawCrc::done)
/// once at the end to finalize.
///
/// # Example
///
/// ```
/// use fwupd::crc::{RawCrc, CrcKind};
///
/// let engine = RawCrc::new(CrcKind::B32Standard);
/// let mut crc = engine.init();
/// crc = engine.step(&[0x01, 0x02, 0x03], crc);
/// crc = engine.step(&[0x04, 0x05, 0x06], crc);
/// crc = engine.step(&[0x07, 0x08, 0x09], crc);
/// let result = engine.done(crc);
/// assert_eq!(result.as_u32(), Some(0x40EFAB9E));
/// ```
#[derive(Clone, Copy)]
pub struct RawCrc {
    kind: CrcKind,
    params: Option<CrcParams>,
    width: Width,
}

impl RawCrc {
    /// Create a new raw CRC engine for the given algorithm.
    pub fn new(kind: CrcKind) -> Self {
        let (params, width) = kind.params_and_width();
        Self {
            kind,
            params,
            width,
        }
    }

    /// Return the initial raw CRC accumulator value for this algorithm.
    ///
    /// Pass this as the `crc` argument to the first call to
    /// [`step()`](RawCrc::step).
    ///
    /// For MISR-16, this returns the `init` value that was passed to
    /// [`CrcKind::Misr16`].
    pub fn init(&self) -> u32 {
        match self.kind {
            CrcKind::Misr16 { init } => init as u32,
            _ => self.params.unwrap().init,
        }
    }

    /// Accumulate a CRC over a buffer using a raw intermediate value.
    ///
    /// The running CRC is carried as a plain `u32` (narrow widths use only the
    /// low bits). Start with [`init()`](RawCrc::init) and pass the return value
    /// of each call as the `crc` argument of the next. When all data has been
    /// fed, pass the final value to [`done()`](RawCrc::done) to apply
    /// reflection and XOR-out.
    ///
    /// # Panics
    ///
    /// Panics if called on a MISR-16 instance with an odd-length buffer.
    pub fn step(&self, buf: &[u8], crc: u32) -> u32 {
        match self.kind {
            CrcKind::Misr16 { .. } => {
                assert!(
                    buf.len().is_multiple_of(2),
                    "MISR-16 requires even-length buffers"
                );
                misr16_step(buf, crc as u16) as u32
            }
            _ => {
                let params = self.params.unwrap();
                match self.width {
                    Width::W32 => raw_step::<u32>(&params, buf, crc),
                    Width::W16 => raw_step::<u16>(&params, buf, crc as u16) as u32,
                    Width::W8 => raw_step::<u8>(&params, buf, crc as u8) as u32,
                }
            }
        }
    }

    /// Finalize a raw intermediate CRC value.
    ///
    /// Applies reflection and XOR-out to a running CRC value obtained from
    /// [`step()`](RawCrc::step) and returns the result as a [`CrcValue`].
    ///
    /// For MISR-16, this is a no-op (returns the accumulator as-is).
    pub fn done(&self, crc: u32) -> CrcValue {
        match self.kind {
            CrcKind::Misr16 { .. } => CrcValue::U16(crc as u16),
            _ => {
                let params = self.params.unwrap();
                match self.width {
                    Width::W32 => CrcValue::U32(raw_done(&params, crc)),
                    Width::W16 => CrcValue::U16(raw_done(&params, crc as u16)),
                    Width::W8 => CrcValue::U8(raw_done(&params, crc as u8)),
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public: CrcState
// ---------------------------------------------------------------------------

/// Internal accumulator state, dispatched by variant.
enum StateInner {
    Fast(crc32fast::Hasher),
    Generic32(u32),
    Generic16(u16),
    Generic8(u8),
    Misr { acc: u16, leftover: Option<u8> },
}

/// In-progress incremental CRC computation.
///
/// Obtained from [`Crc::update()`]. Feed additional data with
/// [`update()`](CrcState::update), then call
/// [`finalize()`](CrcState::finalize) to produce the result.
///
/// # Example
///
/// ```
/// use fwupd::crc::{Crc, CrcKind};
///
/// let crc = Crc::new(CrcKind::B16Usb);
/// let checksum = crc
///     .update(&[0x01, 0x02, 0x03, 0x04, 0x05])
///     .update(&[0x06, 0x07, 0x08, 0x09])
///     .finalize()
///     .unwrap();
/// assert_eq!(checksum.as_u16(), Some(0x4DF1));
/// ```
pub struct CrcState {
    params: Option<CrcParams>,
    inner: StateInner,
}

impl CrcState {
    /// Internal: feed data into the accumulator.
    fn feed(&mut self, buf: &[u8]) {
        match &mut self.inner {
            StateInner::Fast(h) => {
                h.update(buf);
            }
            StateInner::Generic32(crc) => {
                *crc = raw_step(self.params.as_ref().unwrap(), buf, *crc);
            }
            StateInner::Generic16(crc) => {
                *crc = raw_step(self.params.as_ref().unwrap(), buf, *crc);
            }
            StateInner::Generic8(crc) => {
                *crc = raw_step(self.params.as_ref().unwrap(), buf, *crc);
            }
            StateInner::Misr { acc, leftover } => {
                let mut data = buf;

                // If we have a leftover byte from a previous update, pair it
                // with the first byte of this buffer to form a word.
                if let Some(lo) = leftover.take() {
                    if let Some((&hi, rest)) = data.split_first() {
                        let word = u16::from_le_bytes([lo, hi]);
                        *acc = misr16_word_step(*acc, word);
                        data = rest;
                    } else {
                        // buf is empty, put the leftover back
                        *leftover = Some(lo);
                        return;
                    }
                }

                // Process pairs of bytes
                let even_len = data.len() & !1;
                *acc = misr16_step(&data[..even_len], *acc);

                // Save any trailing odd byte for the next update
                if !data.len().is_multiple_of(2) {
                    *leftover = Some(data[data.len() - 1]);
                }
            }
        }
    }

    /// Feed another chunk of data into the CRC computation.
    ///
    /// This method can be called multiple times. When all data has been added,
    /// call [`finalize()`](CrcState::finalize) to get the result.
    pub fn update(mut self, buf: &[u8]) -> Self {
        self.feed(buf);
        self
    }

    /// Finalize the CRC computation and return the result.
    ///
    /// Returns `Err(CrcError::OddLength)` if this is a MISR-16 computation
    /// and the total data fed was an odd number of bytes.
    pub fn finalize(self) -> Result<CrcValue, CrcError> {
        match self.inner {
            StateInner::Fast(h) => Ok(CrcValue::U32(h.finalize())),
            StateInner::Generic32(crc) => {
                Ok(CrcValue::U32(raw_done(self.params.as_ref().unwrap(), crc)))
            }
            StateInner::Generic16(crc) => {
                Ok(CrcValue::U16(raw_done(self.params.as_ref().unwrap(), crc)))
            }
            StateInner::Generic8(crc) => {
                Ok(CrcValue::U8(raw_done(self.params.as_ref().unwrap(), crc)))
            }
            StateInner::Misr { acc, leftover } => {
                if leftover.is_some() {
                    return Err(CrcError::OddLength);
                }
                Ok(CrcValue::U16(acc))
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public: crc32_fast
// ---------------------------------------------------------------------------

/// Hardware-accelerated CRC-32 step with finalized-in, finalized-out semantics.
///
/// Feeds `buf` into a CRC-32 (ISO 3309 / ITU-T V.42) computation seeded with
/// `crc` and returns the finalized result. Pass `0` for the first chunk, then
/// pass the return value of each call as the `crc` argument of the next.
///
/// This is a convenience wrapper around [`crc32fast`] for streaming use where
/// each intermediate value must be a valid, finalized CRC-32.
///
/// # Example
///
/// ```
/// use fwupd::crc::crc32_fast;
///
/// let mut crc = 0u32;
/// crc = crc32_fast(&[0x01, 0x02, 0x03], crc);
/// crc = crc32_fast(&[0x04, 0x05, 0x06], crc);
/// crc = crc32_fast(&[0x07, 0x08, 0x09], crc);
/// assert_eq!(crc, 0x40EFAB9E);
/// ```
pub fn crc32_fast(buf: &[u8], crc: u32) -> u32 {
    let mut hasher = crc32fast::Hasher::new_with_initial(crc);
    hasher.update(buf);
    hasher.finalize()
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_BUF: [u8; 9] = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09];

    // -- CRC-8 --

    #[test]
    fn crc8_standard() {
        let crc = Crc::new(CrcKind::B8Standard);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u8(), Some(0x85));
    }

    #[test]
    fn crc8_wcdma() {
        let crc = Crc::new(CrcKind::B8Wcdma);
        let val = crc.crc(&TEST_BUF).unwrap();
        assert_eq!(val.width(), 8);
        assert!(val.as_u8().is_some());
        assert!(val.as_u16().is_none());
        assert!(val.as_u32().is_none());
    }

    // -- CRC-16 --

    #[test]
    fn crc16_usb() {
        let crc = Crc::new(CrcKind::B16Usb);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u16(), Some(0x4DF1));
    }

    // -- CRC-32 --

    #[test]
    fn crc32_standard() {
        let crc = Crc::new(CrcKind::B32Standard);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0x40EFAB9E));
    }

    #[test]
    fn crc32_bzip2() {
        let crc = Crc::new(CrcKind::B32Bzip2);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0x89AE7A5C));
    }

    #[test]
    fn crc32_jamcrc() {
        let crc = Crc::new(CrcKind::B32Jamcrc);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0xBF105461));
    }

    #[test]
    fn crc32_mpeg2() {
        let crc = Crc::new(CrcKind::B32Mpeg2);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0x765185A3));
    }

    #[test]
    fn crc32_posix() {
        let crc = Crc::new(CrcKind::B32Posix);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0x037915C4));
    }

    #[test]
    fn crc32_sata() {
        let crc = Crc::new(CrcKind::B32Sata);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0xBA55CCAC));
    }

    #[test]
    fn crc32_xfer() {
        let crc = Crc::new(CrcKind::B32Xfer);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0x868E70FC));
    }

    #[test]
    fn crc32c() {
        let crc = Crc::new(CrcKind::B32c);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0x5A14B9F9));
    }

    #[test]
    fn crc32d() {
        let crc = Crc::new(CrcKind::B32d);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0x68AD8D3C));
    }

    #[test]
    fn crc32q() {
        let crc = Crc::new(CrcKind::B32q);
        assert_eq!(crc.crc(&TEST_BUF).unwrap().as_u32(), Some(0xE955C875));
    }

    // -- Incremental (chunked) --

    #[test]
    fn crc32_incremental() {
        let crc = Crc::new(CrcKind::B32Standard);
        let result = crc
            .update(&TEST_BUF[..4])
            .update(&TEST_BUF[4..])
            .finalize()
            .unwrap();
        assert_eq!(result.as_u32(), Some(0x40EFAB9E));
    }

    #[test]
    fn crc16_incremental() {
        let crc = Crc::new(CrcKind::B16Usb);
        let result = crc
            .update(&TEST_BUF[..5])
            .update(&TEST_BUF[5..])
            .finalize()
            .unwrap();
        assert_eq!(result.as_u16(), Some(0x4DF1));
    }

    #[test]
    fn crc8_incremental() {
        let crc = Crc::new(CrcKind::B8Standard);
        let result = crc
            .update(&TEST_BUF[..3])
            .update(&TEST_BUF[3..])
            .finalize()
            .unwrap();
        assert_eq!(result.as_u8(), Some(0x85));
    }

    // -- MISR-16 --

    #[test]
    fn misr16_zero_init() {
        let crc = Crc::new(CrcKind::Misr16 { init: 0x0000 });
        assert_eq!(crc.crc(&TEST_BUF[..8]).unwrap().as_u16(), Some(0x040D));
    }

    #[test]
    fn misr16_ffff_init() {
        let crc = Crc::new(CrcKind::Misr16 { init: 0xFFFF });
        assert_eq!(crc.crc(&TEST_BUF[..8]).unwrap().as_u16(), Some(0xFBFA));
    }

    #[test]
    fn misr16_odd_length() {
        let crc = Crc::new(CrcKind::Misr16 { init: 0 });
        assert_eq!(crc.crc(&TEST_BUF), Err(CrcError::OddLength));
    }

    #[test]
    fn misr16_incremental() {
        let crc = Crc::new(CrcKind::Misr16 { init: 0x0000 });
        // Split [0x01..0x08] into two 4-byte chunks
        let result = crc
            .update(&TEST_BUF[..4])
            .update(&TEST_BUF[4..8])
            .finalize()
            .unwrap();
        assert_eq!(result.as_u16(), Some(0x040D));
    }

    #[test]
    fn misr16_incremental_odd_split() {
        // Split across a word boundary: 3 bytes + 5 bytes = 8 bytes total (even)
        let crc = Crc::new(CrcKind::Misr16 { init: 0x0000 });
        let result = crc
            .update(&TEST_BUF[..3])
            .update(&TEST_BUF[3..8])
            .finalize()
            .unwrap();
        assert_eq!(result.as_u16(), Some(0x040D));
    }

    #[test]
    fn misr16_incremental_odd_total() {
        let crc = Crc::new(CrcKind::Misr16 { init: 0 });
        let result = crc.update(&TEST_BUF).finalize();
        assert_eq!(result, Err(CrcError::OddLength));
    }

    // -- CrcValue accessors --

    #[test]
    fn crc_value_width() {
        assert_eq!(CrcValue::U8(0).width(), 8);
        assert_eq!(CrcValue::U16(0).width(), 16);
        assert_eq!(CrcValue::U32(0).width(), 32);
    }

    #[test]
    fn crc_value_accessors_strict() {
        let v8 = CrcValue::U8(42);
        assert_eq!(v8.as_u8(), Some(42));
        assert_eq!(v8.as_u16(), None);
        assert_eq!(v8.as_u32(), None);

        let v16 = CrcValue::U16(1000);
        assert_eq!(v16.as_u8(), None);
        assert_eq!(v16.as_u16(), Some(1000));
        assert_eq!(v16.as_u32(), None);

        let v32 = CrcValue::U32(0xDEADBEEF);
        assert_eq!(v32.as_u8(), None);
        assert_eq!(v32.as_u16(), None);
        assert_eq!(v32.as_u32(), Some(0xDEADBEEF));
    }

    // -- Crc::kind() and Crc::width() --

    #[test]
    fn crc_kind_roundtrip() {
        let kind = CrcKind::B32Standard;
        let crc = Crc::new(kind);
        assert_eq!(crc.kind(), kind);

        let kind = CrcKind::Misr16 { init: 0x1234 };
        let crc = Crc::new(kind);
        assert_eq!(crc.kind(), kind);
    }

    #[test]
    fn crc_width() {
        assert_eq!(Crc::new(CrcKind::B32Standard).width(), 32);
        assert_eq!(Crc::new(CrcKind::B32c).width(), 32);
        assert_eq!(Crc::new(CrcKind::B16Usb).width(), 16);
        assert_eq!(Crc::new(CrcKind::B16Kermit).width(), 16);
        assert_eq!(Crc::new(CrcKind::B8Standard).width(), 8);
        assert_eq!(Crc::new(CrcKind::B8Wcdma).width(), 8);
        assert_eq!(Crc::new(CrcKind::Misr16 { init: 0 }).width(), 16);
    }

    #[test]
    fn crc_width_matches_value_width() {
        let crc = Crc::new(CrcKind::B32Standard);
        let val = crc.crc(&TEST_BUF).unwrap();
        assert_eq!(crc.width(), val.width());

        let crc = Crc::new(CrcKind::B16Usb);
        let val = crc.crc(&TEST_BUF).unwrap();
        assert_eq!(crc.width(), val.width());

        let crc = Crc::new(CrcKind::B8Standard);
        let val = crc.crc(&TEST_BUF).unwrap();
        assert_eq!(crc.width(), val.width());

        let crc = Crc::new(CrcKind::Misr16 { init: 0 });
        let val = crc.crc(&TEST_BUF[..8]).unwrap();
        assert_eq!(crc.width(), val.width());
    }

    // -- RawCrc (init / step / done) --

    #[test]
    fn raw_crc32_step_done_standard() {
        let engine = RawCrc::new(CrcKind::B32Standard);
        let mut crc = engine.init();
        crc = engine.step(&TEST_BUF[..4], crc);
        crc = engine.step(&TEST_BUF[4..], crc);
        let result = engine.done(crc);
        assert_eq!(result.as_u32(), Some(0x40EFAB9E));
    }

    #[test]
    fn raw_crc32_step_done_bzip2() {
        let engine = RawCrc::new(CrcKind::B32Bzip2);
        let mut crc = engine.init();
        crc = engine.step(&TEST_BUF[..4], crc);
        crc = engine.step(&TEST_BUF[4..], crc);
        let result = engine.done(crc);
        assert_eq!(result.as_u32(), Some(0x89AE7A5C));
    }

    #[test]
    fn raw_crc16_step_done() {
        let engine = RawCrc::new(CrcKind::B16Usb);
        let mut crc = engine.init();
        crc = engine.step(&TEST_BUF[..5], crc);
        crc = engine.step(&TEST_BUF[5..], crc);
        let result = engine.done(crc);
        assert_eq!(result.as_u16(), Some(0x4DF1));
    }

    #[test]
    fn raw_crc8_step_done() {
        let engine = RawCrc::new(CrcKind::B8Standard);
        let mut crc = engine.init();
        crc = engine.step(&TEST_BUF[..3], crc);
        crc = engine.step(&TEST_BUF[3..], crc);
        let result = engine.done(crc);
        assert_eq!(result.as_u8(), Some(0x85));
    }

    #[test]
    fn raw_step_done_matches_oneshot() {
        // Verify RawCrc step+done produces the same result as Crc::crc()
        for &kind in CrcKind::ALL {
            let oneshot = Crc::new(kind).crc(&TEST_BUF).unwrap();

            let engine = RawCrc::new(kind);
            let mut crc = engine.init();
            crc = engine.step(&TEST_BUF, crc);
            let stepped = engine.done(crc);

            assert_eq!(oneshot, stepped, "mismatch for {kind:?}");
        }
    }

    #[test]
    fn raw_step_done_chunked_matches_oneshot() {
        // Verify chunked RawCrc step+done matches Crc::crc()
        for &kind in CrcKind::ALL {
            let oneshot = Crc::new(kind).crc(&TEST_BUF).unwrap();

            let engine = RawCrc::new(kind);
            let mut crc = engine.init();
            crc = engine.step(&TEST_BUF[..4], crc);
            crc = engine.step(&TEST_BUF[4..], crc);
            let stepped = engine.done(crc);

            assert_eq!(oneshot, stepped, "chunked mismatch for {kind:?}");
        }
    }

    // -- crc32_fast --

    #[test]
    fn crc32_fast_chunked() {
        let mut crc = 0u32;
        crc = crc32_fast(&TEST_BUF[..4], crc);
        crc = crc32_fast(&TEST_BUF[4..], crc);
        assert_eq!(crc, 0x40EFAB9E);
    }

    #[test]
    fn crc32_fast_oneshot_matches_crc() {
        let engine = Crc::new(CrcKind::B32Standard);
        let expected = engine.crc(&TEST_BUF).unwrap().as_u32().unwrap();
        assert_eq!(crc32_fast(&TEST_BUF, 0), expected);
    }

    // -- CrcKind::ALL --

    #[test]
    fn all_variants_count() {
        // 10 CRC-32 + 6 CRC-16 + 20 CRC-8 = 36
        assert_eq!(CrcKind::ALL.len(), 36);
    }

    #[test]
    fn all_variants_no_misr16() {
        for kind in CrcKind::ALL {
            assert!(!matches!(kind, CrcKind::Misr16 { .. }));
        }
    }
}
