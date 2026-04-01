//! C-compatible FFI bindings matching the `fu-crc.h` and `fu-crc-private.h` API.

use fwupd::crc::*;

use crate::common::*;

/// Look up CRC parameters and bitwidth for a given `FuCrcKind` value.
///
/// Returns `None` for unknown or out-of-range kinds.
fn crc_lookup(kind: u32) -> Option<(CrcParams, u32)> {
    let (params, bw) = match kind {
        0 => (
            CrcParams {
                poly: 0x00000000,
                init: 0x00000000,
                reflected: true,
                xorout: 0xFFFFFFFF,
            },
            32,
        ),
        1 => (B32Standard::new().params(), 32),
        2 => (B32Bzip2::new().params(), 32),
        3 => (B32Jamcrc::new().params(), 32),
        4 => (B32Mpeg2::new().params(), 32),
        5 => (B32Posix::new().params(), 32),
        6 => (B32Sata::new().params(), 32),
        7 => (B32Xfer::new().params(), 32),
        8 => (B32c::new().params(), 32),
        9 => (B32d::new().params(), 32),
        10 => (B32q::new().params(), 32),
        11 => (B16Xmodem::new().params(), 16),
        12 => (B16Kermit::new().params(), 16),
        13 => (B16Usb::new().params(), 16),
        14 => (B16Umts::new().params(), 16),
        15 => (B16Tms37157::new().params(), 16),
        16 => (B16Bnr::new().params(), 16),
        17 => (B8Wcdma::new().params(), 8),
        18 => (B8Tech3250::new().params(), 8),
        19 => (B8Standard::new().params(), 8),
        20 => (B8SaeJ1850::new().params(), 8),
        21 => (B8Rohc::new().params(), 8),
        22 => (B8Opensafety::new().params(), 8),
        23 => (B8Nrsc5::new().params(), 8),
        24 => (B8MifareMad::new().params(), 8),
        25 => (B8MaximDow::new().params(), 8),
        26 => (B8Lte::new().params(), 8),
        27 => (B8ICode::new().params(), 8),
        28 => (B8Itu::new().params(), 8),
        29 => (B8Hitag::new().params(), 8),
        30 => (B8GsmB::new().params(), 8),
        31 => (B8GsmA::new().params(), 8),
        32 => (B8DvbS2::new().params(), 8),
        33 => (B8Darc::new().params(), 8),
        34 => (B8Cdma2000::new().params(), 8),
        35 => (B8Bluetooth::new().params(), 8),
        36 => (B8Autosar::new().params(), 8),
        _ => return None,
    };
    Some((params, bw))
}

// -- CRC-8 functions --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc8_step(kind: u32, buf: *const u8, bufsz: usize, crc: u8) -> u8 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 8))),
        "invalid CRC-8 kind {kind}"
    );
    let Some((c, 8)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    crc_step(&c, data, crc)
}

#[unsafe(no_mangle)]
pub extern "C" fn fu_crc8_done(kind: u32, crc: u8) -> u8 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 8))),
        "invalid CRC-8 kind {kind}"
    );
    let Some((c, 8)) = crc_lookup(kind) else {
        return 0;
    };
    crc_done(&c, crc)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc8(kind: u32, buf: *const u8, bufsz: usize) -> u8 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 8))),
        "invalid CRC-8 kind {kind}"
    );
    let Some((c, 8)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    crc_done(&c, crc_step(&c, data, u8::from_u32(c.init)))
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
    let Some((c, 8)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { gbytes_to_slice(blob) };
    crc_done(&c, crc_step(&c, data, u8::from_u32(c.init)))
}

// -- CRC-16 functions --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc16_step(kind: u32, buf: *const u8, bufsz: usize, crc: u16) -> u16 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 16))),
        "invalid CRC-16 kind {kind}"
    );
    let Some((c, 16)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    crc_step(&c, data, crc)
}

#[unsafe(no_mangle)]
pub extern "C" fn fu_crc16_done(kind: u32, crc: u16) -> u16 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 16))),
        "invalid CRC-16 kind {kind}"
    );
    let Some((c, 16)) = crc_lookup(kind) else {
        return 0;
    };
    crc_done(&c, crc)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc16(kind: u32, buf: *const u8, bufsz: usize) -> u16 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 16))),
        "invalid CRC-16 kind {kind}"
    );
    let Some((c, 16)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    crc_done(&c, crc_step(&c, data, u16::from_u32(c.init)))
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
    let Some((c, 16)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { gbytes_to_slice(blob) };
    crc_done(&c, crc_step(&c, data, u16::from_u32(c.init)))
}

// -- CRC-32 functions --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc32_step(kind: u32, buf: *const u8, bufsz: usize, crc: u32) -> u32 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 32))),
        "invalid CRC-32 kind {kind}"
    );
    let Some((c, 32)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    crc_step(&c, data, crc)
}

#[unsafe(no_mangle)]
pub extern "C" fn fu_crc32_done(kind: u32, crc: u32) -> u32 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 32))),
        "invalid CRC-32 kind {kind}"
    );
    let Some((c, 32)) = crc_lookup(kind) else {
        return 0;
    };
    crc_done(&c, crc)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc32(kind: u32, buf: *const u8, bufsz: usize) -> u32 {
    debug_assert!(
        matches!(crc_lookup(kind), Some((_, 32))),
        "invalid CRC-32 kind {kind}"
    );
    let Some((c, 32)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { buf_to_slice(buf, bufsz) };
    crc_done(&c, crc_step(&c, data, u32::from_u32(c.init)))
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
    let Some((c, 32)) = crc_lookup(kind) else {
        return 0;
    };
    let data = unsafe { gbytes_to_slice(blob) };
    crc_done(&c, crc_step(&c, data, u32::from_u32(c.init)))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_crc32_fast(buf: *const u8, bufsz: usize, crc: u32) -> u32 {
    unsafe { crc32_z(crc as core::ffi::c_ulong, buf, bufsz) as u32 }
}

// -- Utility functions --

#[unsafe(no_mangle)]
pub extern "C" fn fu_crc_size(kind: u32) -> u32 {
    crc_lookup(kind).map_or(0, |(_, bw)| bw)
}

fn crc_compute(kind: u32, buf: &[u8]) -> Option<u32> {
    let (c, bw) = crc_lookup(kind)?;
    Some(match bw {
        32 => crc_done(&c, crc_step(&c, buf, u32::from_u32(c.init))),
        16 => crc_done(&c, crc_step(&c, buf, u16::from_u32(c.init))) as u32,
        8 => crc_done(&c, crc_step(&c, buf, u8::from_u32(c.init))) as u32,
        _ => return None,
    })
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
    let data = unsafe { buf_to_slice(buf, bufsz) };
    Misr16::new(init).crc(data).unwrap_or(u16::MAX)
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
        let init = crc_lookup(kind).unwrap().0.init;
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
