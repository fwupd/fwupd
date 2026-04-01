/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! C-compatible FFI bindings matching the `fu-crc.h` and `fu-crc-private.h` API.

use fwupd::crc::{Crc, CrcKind, CrcValue, RawCrc, crc32_fast};

use crate::common::*;

/// Check that a read of `n` bytes at `offset` is within a buffer of
/// `bufsz` bytes.  Returns `true` if the read is safe.
fn bounds_check(bufsz: usize, offset: usize, n: usize) -> bool {
    if n == 0 {
        return true;
    }
    match offset.checked_add(n) {
        Some(end) => end <= bufsz,
        None => false,
    }
}

/// Map a C `FuCrcKind` integer to a [`CrcKind`] enum variant.
///
/// Returns `None` for unknown or out-of-range kinds.
/// Kind 0 (`FU_CRC_KIND_UNKNOWN`) is a sentinel value in the C API
/// with no corresponding `CrcKind`.
fn crc_kind_from_int(kind: u32) -> Option<CrcKind> {
    match kind {
        // 0 is FU_CRC_KIND_UNKNOWN -- sentinel, no matching CrcKind
        1 => Some(CrcKind::B32Standard),
        2 => Some(CrcKind::B32Bzip2),
        3 => Some(CrcKind::B32Jamcrc),
        4 => Some(CrcKind::B32Mpeg2),
        5 => Some(CrcKind::B32Posix),
        6 => Some(CrcKind::B32Sata),
        7 => Some(CrcKind::B32Xfer),
        8 => Some(CrcKind::B32c),
        9 => Some(CrcKind::B32d),
        10 => Some(CrcKind::B32q),
        11 => Some(CrcKind::B16Xmodem),
        12 => Some(CrcKind::B16Kermit),
        13 => Some(CrcKind::B16Usb),
        14 => Some(CrcKind::B16Umts),
        15 => Some(CrcKind::B16Tms37157),
        16 => Some(CrcKind::B16Bnr),
        17 => Some(CrcKind::B8Wcdma),
        18 => Some(CrcKind::B8Tech3250),
        19 => Some(CrcKind::B8Standard),
        20 => Some(CrcKind::B8SaeJ1850),
        21 => Some(CrcKind::B8Rohc),
        22 => Some(CrcKind::B8Opensafety),
        23 => Some(CrcKind::B8Nrsc5),
        24 => Some(CrcKind::B8MifareMad),
        25 => Some(CrcKind::B8MaximDow),
        26 => Some(CrcKind::B8Lte),
        27 => Some(CrcKind::B8ICode),
        28 => Some(CrcKind::B8Itu),
        29 => Some(CrcKind::B8Hitag),
        30 => Some(CrcKind::B8GsmB),
        31 => Some(CrcKind::B8GsmA),
        32 => Some(CrcKind::B8DvbS2),
        33 => Some(CrcKind::B8Darc),
        34 => Some(CrcKind::B8Cdma2000),
        35 => Some(CrcKind::B8Bluetooth),
        36 => Some(CrcKind::B8Autosar),
        _ => None,
    }
}

/// Look up a [`CrcKind`] and its bit-width for a given `FuCrcKind` integer.
///
/// Returns `None` for unknown or out-of-range kinds.
/// Kind 0 (`FU_CRC_KIND_UNKNOWN`) is a sentinel value in the C API,
/// not a real algorithm. Returns `None` for it.
fn crc_lookup(kind: u32) -> Option<(CrcKind, u32)> {
    let ck = crc_kind_from_int(kind)?;
    let engine = Crc::new(ck);
    Some((ck, engine.width()))
}

// -- CRC-8 functions --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc8_step(kind: u32, buf: *const u8, bufsz: usize, crc: u8) -> u8 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 8))),
        "invalid CRC-8 kind {kind}"
    );
    let Some((ck, 8)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    RawCrc::new(ck).step(data, crc as u32) as u8
}

#[unsafe(no_mangle)]
pub extern "C" fn fu_crc8_done(kind: u32, crc: u8) -> u8 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 8))),
        "invalid CRC-8 kind {kind}"
    );
    let Some((ck, 8)) = crc_lookup(kind) else {
        return 0;
    };
    match RawCrc::new(ck).done(crc as u32) {
        CrcValue::U8(v) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc8(kind: u32, buf: *const u8, bufsz: usize) -> u8 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 8))),
        "invalid CRC-8 kind {kind}"
    );
    let Some((ck, 8)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    match Crc::new(ck).crc(data) {
        Ok(CrcValue::U8(v)) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc8_bytes(kind: u32, blob: *const GBytes) -> u8 {
    debug_assert!(!blob.is_null(), "blob must not be null");
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 8))),
        "invalid CRC-8 kind {kind}"
    );
    if blob.is_null() {
        return 0;
    }
    let Some((ck, 8)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { gbytes_to_slice(blob) };
    match Crc::new(ck).crc(data) {
        Ok(CrcValue::U8(v)) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc8_safe(
    kind: u32,
    buf: *const u8,
    bufsz: usize,
    offset: usize,
    n: usize,
    value: *mut u8,
    error: *mut *mut GError,
) -> i32 {
    let Some((ck, 8)) = crc_lookup(kind) else {
        return 0;
    };
    if !bounds_check(bufsz, offset, n) {
        unsafe {
            g_set_error_literal(
                error,
                fwupd_error_quark(),
                FWUPD_ERROR_READ,
                c"read bounds check failed".as_ptr(),
            );
        }
        return 0;
    }
    if !value.is_null() {
        let data = unsafe { buf_to_slice(buf.add(offset), n) };
        if let Ok(CrcValue::U8(v)) = Crc::new(ck).crc(data) {
            unsafe { *value = v };
        }
    }
    1
}

// -- CRC-16 functions --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc16_step(kind: u32, buf: *const u8, bufsz: usize, crc: u16) -> u16 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 16))),
        "invalid CRC-16 kind {kind}"
    );
    let Some((ck, 16)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    RawCrc::new(ck).step(data, crc as u32) as u16
}

#[unsafe(no_mangle)]
pub extern "C" fn fu_crc16_done(kind: u32, crc: u16) -> u16 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 16))),
        "invalid CRC-16 kind {kind}"
    );
    let Some((ck, 16)) = crc_lookup(kind) else {
        return 0;
    };
    match RawCrc::new(ck).done(crc as u32) {
        CrcValue::U16(v) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc16(kind: u32, buf: *const u8, bufsz: usize) -> u16 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 16))),
        "invalid CRC-16 kind {kind}"
    );
    let Some((ck, 16)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    match Crc::new(ck).crc(data) {
        Ok(CrcValue::U16(v)) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc16_bytes(kind: u32, blob: *const GBytes) -> u16 {
    debug_assert!(!blob.is_null(), "blob must not be null");
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 16))),
        "invalid CRC-16 kind {kind}"
    );
    if blob.is_null() {
        return 0;
    }
    let Some((ck, 16)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { gbytes_to_slice(blob) };
    match Crc::new(ck).crc(data) {
        Ok(CrcValue::U16(v)) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc16_safe(
    kind: u32,
    buf: *const u8,
    bufsz: usize,
    offset: usize,
    n: usize,
    value: *mut u16,
    error: *mut *mut GError,
) -> i32 {
    let Some((ck, 16)) = crc_lookup(kind) else {
        return 0;
    };
    if !bounds_check(bufsz, offset, n) {
        unsafe {
            g_set_error_literal(
                error,
                fwupd_error_quark(),
                FWUPD_ERROR_READ,
                c"read bounds check failed".as_ptr(),
            );
        }
        return 0;
    }
    if !value.is_null() {
        let data = unsafe { buf_to_slice(buf.add(offset), n) };
        if let Ok(CrcValue::U16(v)) = Crc::new(ck).crc(data) {
            unsafe { *value = v };
        }
    }
    1
}

// -- CRC-32 functions --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc32_step(kind: u32, buf: *const u8, bufsz: usize, crc: u32) -> u32 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 32))),
        "invalid CRC-32 kind {kind}"
    );
    let Some((ck, 32)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    RawCrc::new(ck).step(data, crc)
}

#[unsafe(no_mangle)]
pub extern "C" fn fu_crc32_done(kind: u32, crc: u32) -> u32 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 32))),
        "invalid CRC-32 kind {kind}"
    );
    let Some((ck, 32)) = crc_lookup(kind) else {
        return 0;
    };
    match RawCrc::new(ck).done(crc) {
        CrcValue::U32(v) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc32(kind: u32, buf: *const u8, bufsz: usize) -> u32 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 32))),
        "invalid CRC-32 kind {kind}"
    );
    let Some((ck, 32)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    match Crc::new(ck).crc(data) {
        Ok(CrcValue::U32(v)) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc32_bytes(kind: u32, blob: *const GBytes) -> u32 {
    debug_assert!(!blob.is_null(), "blob must not be null");
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 32))),
        "invalid CRC-32 kind {kind}"
    );
    if blob.is_null() {
        return 0;
    }
    let Some((ck, 32)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { gbytes_to_slice(blob) };
    match Crc::new(ck).crc(data) {
        Ok(CrcValue::U32(v)) => v,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc32_fast(buf: *const u8, bufsz: usize, crc: u32) -> u32 {
    let data = unsafe { buf_to_slice(buf, bufsz) };
    crc32_fast(data, crc)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc32_safe(
    kind: u32,
    buf: *const u8,
    bufsz: usize,
    offset: usize,
    n: usize,
    value: *mut u32,
    error: *mut *mut GError,
) -> i32 {
    let Some((ck, 32)) = crc_lookup(kind) else {
        return 0;
    };
    if !bounds_check(bufsz, offset, n) {
        unsafe {
            g_set_error_literal(
                error,
                fwupd_error_quark(),
                FWUPD_ERROR_READ,
                c"read bounds check failed".as_ptr(),
            );
        }
        return 0;
    }
    if !value.is_null() {
        let data = unsafe { buf_to_slice(buf.add(offset), n) };
        if let Ok(CrcValue::U32(v)) = Crc::new(ck).crc(data) {
            unsafe { *value = v };
        }
    }
    1
}

// -- Utility functions --

#[unsafe(no_mangle)]
pub extern "C" fn fu_crc_size(kind: u32) -> u32 {
    crc_lookup(kind).map_or(0, |(_, bw)| bw)
}

fn crc_compute(kind: u32, buf: &[u8]) -> Option<u32> {
    let (ck, _) = crc_lookup(kind)?;
    match Crc::new(ck).crc(buf) {
        Ok(CrcValue::U32(v)) => Some(v),
        Ok(CrcValue::U16(v)) => Some(v as u32),
        Ok(CrcValue::U8(v)) => Some(v as u32),
        Err(_) => None,
    }
}

fn crc_find(buf: &[u8], crc_target: u32) -> Result<u32, &'static core::ffi::CStr> {
    let mut kind_found: u32 = 0;
    let mut match_cnt: u32 = 0;

    for i in 0..=36u32 {
        let Some((_, bw)) = crc_lookup(i) else {
            continue;
        };

        let fits = match bw {
            32 => true,
            16 => crc_target <= u16::MAX as u32,
            8 => crc_target <= u8::MAX as u32,
            _ => false,
        };
        if !fits {
            continue;
        }

        let Some(computed) = crc_compute(i, buf) else {
            continue;
        };

        let matches = match bw {
            32 => computed == crc_target,
            16 => (computed as u16) == (crc_target as u16),
            8 => (computed as u8) == (crc_target as u8),
            _ => false,
        };

        if matches {
            kind_found = i;
            match_cnt += 1;
        }
    }

    match match_cnt {
        0 => Err(c"no CRC kind matched"),
        1 => Ok(kind_found),
        _ => Err(c"multiple CRC kinds matched"),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc_find(
    buf: *const u8,
    bufsz: usize,
    crc_target: u32,
    kind: *mut u32,
    error: *mut *mut GError,
) -> i32 {
    let data = unsafe { buf_to_slice(buf, bufsz) };
    match crc_find(data, crc_target) {
        Ok(k) => {
            if !kind.is_null() {
                unsafe { *kind = k };
            }
            1
        }
        Err(msg) => {
            unsafe {
                g_set_error_literal(
                    error,
                    fwupd_error_quark(),
                    FWUPD_ERROR_NOT_FOUND,
                    msg.as_ptr(),
                );
            }
            0
        }
    }
}

// -- MISR-16 --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc_misr16(init: u16, buf: *const u8, bufsz: usize) -> u16 {
    debug_assert!(!buf.is_null(), "buf must not be null");
    debug_assert!(bufsz.is_multiple_of(2), "bufsz must be even, got {bufsz}");
    if buf.is_null() || !bufsz.is_multiple_of(2) {
        return u16::MAX;
    }
    let engine = Crc::new(CrcKind::Misr16 { init });
    let data = unsafe { buf_to_slice(buf, bufsz) };
    match engine.crc(data) {
        Ok(CrcValue::U16(v)) => v,
        _ => u16::MAX,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_BUF: [u8; 9] = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09];

    const FU_CRC_KIND_B32_STANDARD: u32 = 1;
    const FU_CRC_KIND_B32_BZIP2: u32 = 2;
    const FU_CRC_KIND_B32_JAMCRC: u32 = 3;
    const FU_CRC_KIND_B32_MPEG2: u32 = 4;
    const FU_CRC_KIND_B32_POSIX: u32 = 5;
    const FU_CRC_KIND_B32_SATA: u32 = 6;
    const FU_CRC_KIND_B32_XFER: u32 = 7;
    const FU_CRC_KIND_B32C: u32 = 8;
    const FU_CRC_KIND_B32D: u32 = 9;
    const FU_CRC_KIND_B32Q: u32 = 10;
    const FU_CRC_KIND_B16_USB: u32 = 13;
    const FU_CRC_KIND_B8_STANDARD: u32 = 19;

    #[test]
    fn ffi_crc8_standard() {
        let result = unsafe { fu_crc8(FU_CRC_KIND_B8_STANDARD, TEST_BUF.as_ptr(), TEST_BUF.len()) };
        assert_eq!(result, 0x85u8);
    }

    #[test]
    fn ffi_crc16_usb() {
        let result = unsafe { fu_crc16(FU_CRC_KIND_B16_USB, TEST_BUF.as_ptr(), TEST_BUF.len()) };
        assert_eq!(result, 0x4DF1u16);
    }

    #[test]
    fn ffi_crc32_all() {
        let cases: &[(u32, u32)] = &[
            (FU_CRC_KIND_B32_STANDARD, 0x40EFAB9E),
            (FU_CRC_KIND_B32_BZIP2, 0x89AE7A5C),
            (FU_CRC_KIND_B32_JAMCRC, 0xBF105461),
            (FU_CRC_KIND_B32_MPEG2, 0x765185A3),
            (FU_CRC_KIND_B32_POSIX, 0x037915C4),
            (FU_CRC_KIND_B32_SATA, 0xBA55CCAC),
            (FU_CRC_KIND_B32_XFER, 0x868E70FC),
            (FU_CRC_KIND_B32C, 0x5A14B9F9),
            (FU_CRC_KIND_B32D, 0x68AD8D3C),
            (FU_CRC_KIND_B32Q, 0xE955C875),
        ];
        for &(kind, expected) in cases {
            let result = unsafe { fu_crc32(kind, TEST_BUF.as_ptr(), TEST_BUF.len()) };
            assert_eq!(result, expected, "CRC-32 kind={kind} failed");
        }
    }

    #[test]
    fn ffi_crc32_step_done() {
        let kind = FU_CRC_KIND_B32_STANDARD;
        let ck = crc_lookup(kind).unwrap().0;
        let init = RawCrc::new(ck).init();
        let crc = unsafe { fu_crc32_step(kind, TEST_BUF[..4].as_ptr(), 4, init) };
        let crc = unsafe { fu_crc32_step(kind, TEST_BUF[4..].as_ptr(), 5, crc) };
        let result = fu_crc32_done(kind, crc);
        assert_eq!(result, 0x40EFAB9E);
    }

    #[test]
    fn ffi_crc_size() {
        assert_eq!(fu_crc_size(FU_CRC_KIND_B32_STANDARD), 32);
        assert_eq!(fu_crc_size(FU_CRC_KIND_B16_USB), 16);
        assert_eq!(fu_crc_size(FU_CRC_KIND_B8_STANDARD), 8);
        assert_eq!(fu_crc_size(FU_CRC_KIND_B32Q), 32);
        assert_eq!(fu_crc_size(37), 0);
    }

    #[test]
    #[cfg(not(debug_assertions))]
    fn ffi_invalid_kind() {
        let result = unsafe { fu_crc32(99, TEST_BUF.as_ptr(), TEST_BUF.len()) };
        assert_eq!(result, 0);
        let result =
            unsafe { fu_crc32(FU_CRC_KIND_B8_STANDARD, TEST_BUF.as_ptr(), TEST_BUF.len()) };
        assert_eq!(result, 0);
    }

    #[test]
    fn crc_find_rust_api() {
        assert_eq!(
            crc_find(&TEST_BUF, 0x40EFAB9E),
            Ok(FU_CRC_KIND_B32_STANDARD)
        );
        assert_eq!(crc_find(&TEST_BUF, 0x4DF1), Ok(FU_CRC_KIND_B16_USB));
        assert_eq!(crc_find(&TEST_BUF, 0xDEADBEEF), Err(c"no CRC kind matched"));
    }

    #[test]
    fn ffi_misr16_zero_init() {
        let result = unsafe { fu_crc_misr16(0x0000, TEST_BUF[..8].as_ptr(), 8) };
        assert_eq!(result, 0x040D);
    }

    #[test]
    fn ffi_misr16_ffff_init() {
        let result = unsafe { fu_crc_misr16(0xFFFF, TEST_BUF[..8].as_ptr(), 8) };
        assert_eq!(result, 0xFBFA);
    }

    #[test]
    #[cfg(not(debug_assertions))]
    fn ffi_misr16_odd_length() {
        let result = unsafe { fu_crc_misr16(0, TEST_BUF.as_ptr(), 9) };
        assert_eq!(result, u16::MAX);
    }

    #[test]
    #[cfg(not(debug_assertions))]
    fn ffi_misr16_null_buf() {
        let result = unsafe { fu_crc_misr16(0, core::ptr::null(), 8) };
        assert_eq!(result, u16::MAX);
    }
}
