//! C-compatible FFI bindings matching the `fu-xor.h` API.

use fwupd::xor::xor8;

use crate::common::*;

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_xor8(buf: *const u8, bufsz: usize) -> u8 {
    debug_assert!(!buf.is_null() || bufsz == 0, "buf must not be null");
    if buf.is_null() {
        return u8::MAX;
    }
    let data = unsafe { buf_to_slice(buf, bufsz) };
    xor8(data)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_xor8_safe(
    buf: *const u8,
    bufsz: usize,
    offset: usize,
    n: usize,
    value: *mut u8,
    error: *mut *mut GError,
) -> i32 {
    debug_assert!(!buf.is_null(), "buf must not be null");
    if buf.is_null() {
        return 0;
    }
    let data = unsafe { buf_to_slice(buf, bufsz) };
    let cur = if !value.is_null() { unsafe { *value } } else { 0 };
    match fwupd::xor::xor8_safe(data, offset, n, cur) {
        Ok(result) => {
            if !value.is_null() {
                unsafe { *value = result };
            }
            1
        }
        Err(_) => {
            unsafe {
                g_set_error_literal(
                    error,
                    fwupd_error_quark(),
                    FWUPD_ERROR_READ,
                    c"XOR read out of bounds".as_ptr(),
                );
            }
            0
        }
    }
}
