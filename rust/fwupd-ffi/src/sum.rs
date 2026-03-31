//! C-compatible FFI bindings matching the `fu-sum.h` API.

use fwupd::sum::*;

use crate::common::*;

const G_BIG_ENDIAN: u32 = 4321;

fn endian_from_c(endian: u32) -> Endian {
    match endian {
        G_BIG_ENDIAN => Endian::Big,
        _ => Endian::Little,
    }
}

// -- sum8 --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum8(buf: *const u8, bufsz: usize) -> u8 {
    debug_assert!(!buf.is_null() || bufsz == 0);
    if buf.is_null() { return u8::MAX }
    sum8(unsafe { buf_to_slice(buf, bufsz) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum8_safe(
    buf: *const u8, bufsz: usize, offset: usize, n: usize,
    value: *mut u8, error: *mut *mut GError,
) -> i32 {
    debug_assert!(!buf.is_null());
    if buf.is_null() { return 0 }
    let data = unsafe { buf_to_slice(buf, bufsz) };
    match sum8_safe(data, offset, n) {
        Ok(result) => {
            if !value.is_null() { unsafe { *value = result } }
            1
        }
        Err(_) => {
            unsafe { g_set_error_literal(error, fwupd_error_quark(), FWUPD_ERROR_READ, c"sum8 read out of bounds".as_ptr()) }
            0
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum8_bytes(blob: *const GBytes) -> u8 {
    debug_assert!(!blob.is_null());
    if blob.is_null() { return u8::MAX }
    let data = unsafe { gbytes_to_slice(blob) };
    if data.is_empty() { return 0 }
    sum8(data)
}

// -- sum16 --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum16(buf: *const u8, bufsz: usize) -> u16 {
    debug_assert!(!buf.is_null() || bufsz == 0);
    if buf.is_null() { return u16::MAX }
    sum16(unsafe { buf_to_slice(buf, bufsz) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum16_safe(
    buf: *const u8, bufsz: usize, offset: usize, n: usize,
    value: *mut u16, error: *mut *mut GError,
) -> i32 {
    debug_assert!(!buf.is_null());
    if buf.is_null() { return 0 }
    let data = unsafe { buf_to_slice(buf, bufsz) };
    match sum16_safe(data, offset, n) {
        Ok(result) => {
            if !value.is_null() { unsafe { *value = result } }
            1
        }
        Err(_) => {
            unsafe { g_set_error_literal(error, fwupd_error_quark(), FWUPD_ERROR_READ, c"sum16 read out of bounds".as_ptr()) }
            0
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum16_bytes(blob: *const GBytes) -> u16 {
    debug_assert!(!blob.is_null());
    if blob.is_null() { return u16::MAX }
    sum16(unsafe { gbytes_to_slice(blob) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum16w(buf: *const u8, bufsz: usize, endian: u32) -> u16 {
    debug_assert!(!buf.is_null() || bufsz == 0);
    if buf.is_null() { return u16::MAX }
    let data = unsafe { buf_to_slice(buf, bufsz) };
    sum16w(data, endian_from_c(endian)).unwrap_or(u16::MAX)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum16w_bytes(blob: *const GBytes, endian: u32) -> u16 {
    debug_assert!(!blob.is_null());
    if blob.is_null() { return u16::MAX }
    let data = unsafe { gbytes_to_slice(blob) };
    sum16w(data, endian_from_c(endian)).unwrap_or(u16::MAX)
}

// -- sum32 --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum32(buf: *const u8, bufsz: usize) -> u32 {
    debug_assert!(!buf.is_null() || bufsz == 0);
    if buf.is_null() { return u32::MAX }
    sum32(unsafe { buf_to_slice(buf, bufsz) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum32_bytes(blob: *const GBytes) -> u32 {
    debug_assert!(!blob.is_null());
    if blob.is_null() { return u32::MAX }
    sum32(unsafe { gbytes_to_slice(blob) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum32w(buf: *const u8, bufsz: usize, endian: u32) -> u32 {
    debug_assert!(!buf.is_null() || bufsz == 0);
    if buf.is_null() { return u32::MAX }
    let data = unsafe { buf_to_slice(buf, bufsz) };
    sum32w(data, endian_from_c(endian)).unwrap_or(u32::MAX)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_sum32w_bytes(blob: *const GBytes, endian: u32) -> u32 {
    debug_assert!(!blob.is_null());
    if blob.is_null() { return u32::MAX }
    let data = unsafe { gbytes_to_slice(blob) };
    sum32w(data, endian_from_c(endian)).unwrap_or(u32::MAX)
}
