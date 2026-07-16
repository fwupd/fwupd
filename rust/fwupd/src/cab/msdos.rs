/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use crate::declare_bitflags;
use zerocopy::byteorder::little_endian::U16 as U16LE;
use zerocopy::{FromBytes, Immutable, IntoBytes, KnownLayout};

/// An MS-DOS date packed into a 16-bit little endian value.
///
/// The bit layout is:
///
/// | Bits  | Field        | Range    |
/// |-------|--------------|----------|
/// | 15--9 | year − 1980  | 0--127   |
/// | 8--5  | month        | 1--12    |
/// | 4--0  | day          | 1--31    |
///
/// Use [`From<u16>`] to construct a value and the accessor methods to
/// extract the individual fields.
#[derive(Debug, Default, Clone, Copy, FromBytes, IntoBytes, KnownLayout, PartialEq, Immutable)]
pub struct MsDosDate(U16LE);

impl MsDosDate {
    /// Create a date from individual components.
    ///
    /// `year` is clamped to the representable range 1980--2107.
    #[must_use]
    pub fn from_ymd(year: u16, month: u8, day: u8) -> Self {
        let y: u16 = year.saturating_sub(1980).min(127);
        let m: u16 = u16::from(month) & 0xf;
        let d: u16 = u16::from(day) & 0x1f;
        MsDosDate(U16LE::new((y << 9) | (m << 5) | d))
    }

    /// Returns the year (1980--2107).
    #[must_use]
    pub fn year(&self) -> u16 {
        (self.0.get() >> 9) + 1980
    }

    /// Returns the month (1--12).
    #[must_use]
    pub fn month(&self) -> u8 {
        (self.0.get() >> 5 & 0xf) as u8
    }

    /// Returns the day of the month (1--31).
    #[must_use]
    pub fn day(&self) -> u8 {
        (self.0.get() & 0x1f) as u8
    }
}

impl From<u16> for MsDosDate {
    fn from(v: u16) -> MsDosDate {
        MsDosDate(U16LE::new(v))
    }
}

impl From<MsDosDate> for u16 {
    fn from(d: MsDosDate) -> u16 {
        d.0.get()
    }
}

impl std::fmt::Display for MsDosDate {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let (y, m, d) = (self.year(), self.month(), self.day());
        write!(f, "{y}-{m:02}-{d:02}")
    }
}

/// An MS-DOS time-of-day packed into a 16-bit little endian value.
///
/// The bit layout is:
///
/// | Bits  | Field      | Range  |
/// |-------|------------|--------|
/// | 15--11| hour       | 0--23  |
/// | 10--5 | minute     | 0--59  |
/// | 4--0  | second / 2 | 0--29  |
///
/// Seconds have 2-second resolution, only even seconds are returned.
/// by [`second()`](MsDosTime::second).
#[derive(Debug, Default, Clone, Copy, FromBytes, IntoBytes, KnownLayout, PartialEq, Immutable)]
pub struct MsDosTime(U16LE);

impl MsDosTime {
    /// Create a time from individual components.
    ///
    /// `second` is stored with 2-second resolution (divided by 2).
    #[must_use]
    pub fn from_hms(hour: u8, minute: u8, second: u8) -> Self {
        let h: u16 = u16::from(hour) & 0x1f;
        let m: u16 = u16::from(minute) & 0x3f;
        let s: u16 = (u16::from(second) / 2) & 0x1f;
        MsDosTime(U16LE::new(h << 11 | m << 5 | s))
    }

    /// Returns the hour (0--23).
    #[must_use]
    pub fn hour(&self) -> u8 {
        (self.0.get() >> 11) as u8
    }

    /// Returns the minute (0--59).
    #[must_use]
    pub fn minute(&self) -> u8 {
        (self.0.get() >> 5 & 0x3f) as u8
    }

    /// Returns the second (0--58, always even due to 2-second resolution).
    #[must_use]
    pub fn second(&self) -> u8 {
        (((self.0.get() & 0x1f) * 2) & 0xff) as u8
    }

    /// Returns the raw packed 16-bit value.
    #[must_use]
    pub fn packed(&self) -> u16 {
        self.0.get()
    }
}

impl From<u16> for MsDosTime {
    fn from(v: u16) -> MsDosTime {
        MsDosTime(U16LE::new(v))
    }
}

impl From<MsDosTime> for u16 {
    fn from(d: MsDosTime) -> u16 {
        d.0.get()
    }
}

impl std::fmt::Display for MsDosTime {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let (h, m, s) = (self.hour(), self.minute(), self.second());
        write!(f, "{h:02}:{m:02}:{s:02}")
    }
}

declare_bitflags! {
    /// MS-DOS / MS Cabinet file attributes stored as a 16-bit bitmask.
    ///
    /// Standard MS-DOS attributes occupy the low byte.  The MS Cabinet
    /// specification adds `EXEC` (bit 6) and `NAME_IS_UTF` (bit 7).
    ///
    /// | Bit | Constant          | Meaning                          |
    /// |-----|-------------------|----------------------------------|
    /// | 0   | `RDONLY`          | read-only                        |
    /// | 1   | `HIDDEN`          | hidden                           |
    /// | 2   | `SYSTEM`          | system file                      |
    /// | 5   | `ARCH`            | modified since last backup        |
    /// | 6   | `EXEC`            | run after extraction (CAB-only)  |
    /// | 7   | `NAME_IS_UTF`     | filename is UTF-8 (CAB-only)     |
    #[derive(Debug, Clone, Copy, FromBytes, IntoBytes, KnownLayout, PartialEq, Immutable)]
    pub struct MsDosFileAttrs / MsDosFileAttr : u16 {
        /// Read-only (`_A_RDONLY`, bit 0).
        ReadOnly = 0x01;
        /// Hidden (`_A_HIDDEN`, bit 1).
        Hidden = 0x02;
        /// System file (`_A_SYSTEM`, bit 2).
        System = 0x04;
        /// Modified since last backup (`_A_ARCH`, bit 5).
        Arch = 0x20;
        /// Run after extraction (`_A_EXEC`, bit 6). CAB-only.
        Exec = 0x40;
        /// Filename is UTF-8 (`_A_NAME_IS_UTF`, bit 7). CAB-only.
        NameIsUtf  = 0x80;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bitflags::Bitflags;

    #[test]
    fn msdos_date_known_value() {
        // 2023-06-15: year offset = 43, month = 6, day = 15
        // packed = (43 << 9) | (6 << 5) | 15 = 0x56CF
        let d = MsDosDate::from(0x56CF);
        assert_eq!(d.year(), 2023);
        assert_eq!(d.month(), 6);
        assert_eq!(d.day(), 15);

        // another random date
        let d = MsDosDate::from_ymd(2013, 12, 4);
        assert_eq!(d.year(), 2013);
        assert_eq!(d.month(), 12);
        assert_eq!(d.day(), 4);
    }

    #[test]
    fn msdos_date_epoch() {
        // 1980-01-01: the earliest representable date
        let d = MsDosDate::from((1 << 5) | 1);
        assert_eq!(d.year(), 1980);
        assert_eq!(d.month(), 1);
        assert_eq!(d.day(), 1);
    }

    #[test]
    fn msdos_date_max() {
        // 2107-12-31: the latest representable date
        let d = MsDosDate::from((127 << 9) | (12 << 5) | 31);
        assert_eq!(d.year(), 2107);
        assert_eq!(d.month(), 12);
        assert_eq!(d.day(), 31);
    }

    #[test]
    fn msdos_date_day_uses_five_bits() {
        // day = 31 (0x1f) needs all five bits
        let d = MsDosDate::from(31);
        assert_eq!(d.day(), 31);
    }

    #[test]
    fn msdos_date_roundtrip() {
        let raw: u16 = 0x56CF;
        let d = MsDosDate::from(raw);
        let back: u16 = d.into();
        assert_eq!(raw, back);
    }

    #[test]
    fn msdos_date_zero() {
        let d = MsDosDate::from(0u16);
        assert_eq!(d.year(), 1980);
        assert_eq!(d.month(), 0);
        assert_eq!(d.day(), 0);
    }

    // -----------------------------------------------------------------------
    // MsDosTime
    // -----------------------------------------------------------------------

    #[test]
    fn msdos_time_known_value() {
        // 14:35:42  ->  hour=14, minute=35, second/2=21
        // packed = (14 << 11) | (35 << 5) | 21 = 28672 + 1120 + 21 = 0x7475
        let t = MsDosTime::from(0x7475);
        assert_eq!(t.hour(), 14);
        assert_eq!(t.minute(), 35);
        assert_eq!(t.second(), 42);

        let d = MsDosTime::from_hms(15, 20, 58);
        assert_eq!(d.hour(), 15);
        assert_eq!(d.minute(), 20);
        assert_eq!(d.second(), 58);
    }

    #[test]
    fn msdos_time_midnight() {
        let t = MsDosTime::from(0u16);
        assert_eq!(t.hour(), 0);
        assert_eq!(t.minute(), 0);
        assert_eq!(t.second(), 0);
    }

    #[test]
    #[allow(clippy::decimal_bitwise_operands)]
    fn msdos_time_max() {
        // 23:59:58 (max representable)
        let t = MsDosTime::from((23 << 11) | (59 << 5) | 29);
        assert_eq!(t.hour(), 23);
        assert_eq!(t.minute(), 59);
        assert_eq!(t.second(), 58);
    }

    #[test]
    fn msdos_time_minute_uses_six_bits() {
        // minute = 59 (0x3B) needs six bits
        let t = MsDosTime::from(59u16 << 5);
        assert_eq!(t.minute(), 59);
    }

    #[test]
    fn msdos_time_second_uses_five_bits() {
        // second/2 = 29 (0x1D) needs five bits -> second = 58
        let t = MsDosTime::from(29u16);
        assert_eq!(t.second(), 58);
    }

    #[test]
    fn msdos_time_packed() {
        let raw: u16 = 0x7475;
        let t = MsDosTime::from(raw);
        assert_eq!(t.packed(), raw);
    }

    #[test]
    fn msdos_time_roundtrip() {
        let raw: u16 = 0x7475;
        let t = MsDosTime::from(raw);
        let back: u16 = t.into();
        assert_eq!(raw, back);
    }

    // -----------------------------------------------------------------------
    // MsDosFileAttr
    // -----------------------------------------------------------------------

    #[test]
    fn msdos_fileattr_none() {
        let a = MsDosFileAttrs::empty();
        assert!(!a.contains(MsDosFileAttr::ReadOnly));
        assert!(!a.contains(MsDosFileAttr::Hidden));
        assert!(!a.contains(MsDosFileAttr::System));
        assert!(!a.contains(MsDosFileAttr::Arch));
        assert!(!a.contains(MsDosFileAttr::Exec));
        assert!(!a.contains(MsDosFileAttr::NameIsUtf));
    }

    #[test]
    fn msdos_fileattr_rdonly() {
        let a = MsDosFileAttr::ReadOnly.as_mask();
        assert!(a.contains(MsDosFileAttr::ReadOnly));
        assert!(!a.contains(MsDosFileAttr::Hidden));
    }

    #[test]
    fn msdos_fileattr_hidden() {
        let a = MsDosFileAttr::Hidden.as_mask();
        assert!(a.contains(MsDosFileAttr::Hidden));
        assert!(!a.contains(MsDosFileAttr::ReadOnly));
    }

    #[test]
    fn msdos_fileattr_system() {
        assert!(MsDosFileAttr::System
            .as_mask()
            .contains(MsDosFileAttr::System));
    }

    #[test]
    fn msdos_fileattr_archive() {
        assert!(MsDosFileAttr::Arch.as_mask().contains(MsDosFileAttr::Arch));
    }

    #[test]
    fn msdos_fileattr_exec() {
        assert!(MsDosFileAttr::Exec.as_mask().contains(MsDosFileAttr::Exec));
    }

    #[test]
    fn msdos_fileattr_utf8() {
        assert!(MsDosFileAttr::NameIsUtf
            .as_mask()
            .contains(MsDosFileAttr::NameIsUtf));
    }

    #[test]
    fn msdos_fileattr_combined() {
        // rdonly + archive + utf8 = 0x01 | 0x20 | 0x80 = 0xA1
        let a = MsDosFileAttr::ReadOnly | MsDosFileAttr::Arch | MsDosFileAttr::NameIsUtf;
        assert!(a.contains(MsDosFileAttr::ReadOnly));
        assert!(!a.contains(MsDosFileAttr::Hidden));
        assert!(!a.contains(MsDosFileAttr::System));
        assert!(a.contains(MsDosFileAttr::Arch));
        assert!(!a.contains(MsDosFileAttr::Exec));
        assert!(a.contains(MsDosFileAttr::NameIsUtf));
    }

    #[test]
    fn msdos_fileattr_roundtrip() {
        let raw: u16 = 0xA1;
        let a = MsDosFileAttrs::from_bits(raw);
        assert_eq!(a.bits(), raw);
    }
}
