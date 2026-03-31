//! C-compatible FFI bindings matching the `fu-mem.h` and `fu-mem-private.h` API.

use fwupd::mem::{Endian, MemErrorKind};

use crate::common::*;

const G_BIG_ENDIAN: u32 = 4321;

fn error_code_from_kind(kind: MemErrorKind) -> i32 {
    match kind {
        MemErrorKind::Read => FWUPD_ERROR_READ,
        MemErrorKind::Write => FWUPD_ERROR_WRITE,
        MemErrorKind::InvalidData => FWUPD_ERROR_INVALID_DATA,
        MemErrorKind::NotFound => FWUPD_ERROR_NOT_FOUND,
        MemErrorKind::NotSupported => FWUPD_ERROR_NOT_SUPPORTED,
    }
}

fn endian_from_c(endian: u32) -> Endian {
    match endian {
        G_BIG_ENDIAN => Endian::Big,
        _ => Endian::Little,
    }
}

macro_rules! set_gerror {
    ($error:expr, $code:expr, $msg:expr) => {
        if !$error.is_null() {
            unsafe { g_set_error_literal($error, fwupd_error_quark(), $code, $msg) }
        }
    };
}

// -- memchk --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memchk_read(
    bufsz: usize,
    offset: usize,
    n: usize,
    error: *mut *mut GError,
) -> i32 {
    match fwupd::mem::memchk_read(bufsz, offset, n) {
        Ok(()) => 1,
        Err(_) => {
            set_gerror!(error, FWUPD_ERROR_READ, c"read bounds check failed".as_ptr());
            0
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memchk_write(
    bufsz: usize,
    offset: usize,
    n: usize,
    error: *mut *mut GError,
) -> i32 {
    match fwupd::mem::memchk_write(bufsz, offset, n) {
        Ok(()) => 1,
        Err(_) => {
            set_gerror!(error, FWUPD_ERROR_WRITE, c"write bounds check failed".as_ptr());
            0
        }
    }
}

// -- memcpy / memcmp / memmem --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memcpy_safe(
    dst: *mut u8,
    dst_sz: usize,
    dst_offset: usize,
    src: *const u8,
    src_sz: usize,
    src_offset: usize,
    n: usize,
    error: *mut *mut GError,
) -> i32 {
    debug_assert!(!dst.is_null() && !src.is_null());
    if dst.is_null() || src.is_null() {
        return 0;
    }
    let dst_slice = unsafe { buf_to_slice_mut(dst, dst_sz) };
    let src_slice = unsafe { buf_to_slice(src, src_sz) };
    match fwupd::mem::memcpy_safe(dst_slice, dst_offset, src_slice, src_offset, n) {
        Ok(()) => 1,
        Err(e) => {
            set_gerror!(error, error_code_from_kind(e.kind), c"memcpy bounds check failed".as_ptr());
            0
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memcmp_safe(
    buf1: *const u8,
    buf1_sz: usize,
    buf1_offset: usize,
    buf2: *const u8,
    buf2_sz: usize,
    buf2_offset: usize,
    n: usize,
    error: *mut *mut GError,
) -> i32 {
    debug_assert!(!buf1.is_null() && !buf2.is_null());
    if buf1.is_null() || buf2.is_null() {
        return 0;
    }
    let s1 = unsafe { buf_to_slice(buf1, buf1_sz) };
    let s2 = unsafe { buf_to_slice(buf2, buf2_sz) };
    match fwupd::mem::memcmp_safe(s1, buf1_offset, s2, buf2_offset, n) {
        Ok(()) => 1,
        Err(e) => {
            set_gerror!(error, error_code_from_kind(e.kind), c"memory comparison failed".as_ptr());
            0
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memmem_safe(
    haystack: *const u8,
    haystack_sz: usize,
    needle: *const u8,
    needle_sz: usize,
    offset: *mut usize,
    error: *mut *mut GError,
) -> i32 {
    debug_assert!(!haystack.is_null() && !needle.is_null());
    if haystack.is_null() || needle.is_null() {
        return 0;
    }
    let h = unsafe { buf_to_slice(haystack, haystack_sz) };
    let n = unsafe { buf_to_slice(needle, needle_sz) };
    match fwupd::mem::memmem_safe(h, n) {
        Ok(off) => {
            if !offset.is_null() {
                unsafe { *offset = off }
            }
            1
        }
        Err(e) => {
            set_gerror!(error, error_code_from_kind(e.kind), c"needle not found in haystack".as_ptr());
            0
        }
    }
}

// -- memdup / memstrsafe --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memdup_safe(
    src: *const u8,
    n: usize,
    error: *mut *mut GError,
) -> *mut u8 {
    if n > 0x40000000 {
        set_gerror!(error, FWUPD_ERROR_NOT_SUPPORTED, c"allocation too large".as_ptr());
        return core::ptr::null_mut();
    }
    if src.is_null() && n > 0 {
        set_gerror!(error, FWUPD_ERROR_READ, c"source is null".as_ptr());
        return core::ptr::null_mut();
    }
    unsafe { g_memdup2(src, n) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memstrsafe(
    buf: *const u8,
    bufsz: usize,
    offset: usize,
    maxsz: usize,
    error: *mut *mut GError,
) -> *mut core::ffi::c_char {
    debug_assert!(!buf.is_null());
    if buf.is_null() {
        return core::ptr::null_mut();
    }
    if fwupd::mem::memchk_read(bufsz, offset, maxsz).is_err() {
        set_gerror!(error, FWUPD_ERROR_READ, c"read bounds check failed".as_ptr());
        return core::ptr::null_mut();
    }
    let ptr = unsafe { buf.add(offset) as *const core::ffi::c_char };
    let result = unsafe { fu_strsafe(ptr, maxsz) };
    if result.is_null() {
        set_gerror!(error, FWUPD_ERROR_INVALID_DATA, c"invalid ASCII string".as_ptr());
        return core::ptr::null_mut();
    }
    result
}

// -- memread (non-safe) --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memread_uint16(buf: *const u8, endian: u32) -> u16 {
    debug_assert!(!buf.is_null());
    fwupd::mem::memread_uint16(unsafe { buf_to_slice(buf, 2) }, endian_from_c(endian))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memread_uint24(buf: *const u8, endian: u32) -> u32 {
    debug_assert!(!buf.is_null());
    fwupd::mem::memread_uint24(unsafe { buf_to_slice(buf, 3) }, endian_from_c(endian))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memread_uint32(buf: *const u8, endian: u32) -> u32 {
    debug_assert!(!buf.is_null());
    fwupd::mem::memread_uint32(unsafe { buf_to_slice(buf, 4) }, endian_from_c(endian))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memread_uint64(buf: *const u8, endian: u32) -> u64 {
    debug_assert!(!buf.is_null());
    fwupd::mem::memread_uint64(unsafe { buf_to_slice(buf, 8) }, endian_from_c(endian))
}

// -- memwrite (non-safe) --

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memwrite_uint16(buf: *mut u8, val: u16, endian: u32) {
    debug_assert!(!buf.is_null());
    fwupd::mem::memwrite_uint16(
        unsafe { buf_to_slice_mut(buf, 2) },
        val,
        endian_from_c(endian),
    );
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memwrite_uint24(buf: *mut u8, val: u32, endian: u32) {
    debug_assert!(!buf.is_null());
    fwupd::mem::memwrite_uint24(
        unsafe { buf_to_slice_mut(buf, 3) },
        val,
        endian_from_c(endian),
    );
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memwrite_uint32(buf: *mut u8, val: u32, endian: u32) {
    debug_assert!(!buf.is_null());
    fwupd::mem::memwrite_uint32(
        unsafe { buf_to_slice_mut(buf, 4) },
        val,
        endian_from_c(endian),
    );
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memwrite_uint64(buf: *mut u8, val: u64, endian: u32) {
    debug_assert!(!buf.is_null());
    fwupd::mem::memwrite_uint64(
        unsafe { buf_to_slice_mut(buf, 8) },
        val,
        endian_from_c(endian),
    );
}

// -- memread_safe --

macro_rules! memread_safe_fn {
    ($name:ident, $t:ty, $fn:path) => {
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn $name(
            buf: *const u8, bufsz: usize, offset: usize,
            value: *mut $t, endian: u32, error: *mut *mut GError,
        ) -> i32 {
            debug_assert!(!buf.is_null());
            if buf.is_null() { return 0 }
            let data = unsafe { buf_to_slice(buf, bufsz) };
            match $fn(data, offset, endian_from_c(endian)) {
                Ok(v) => { if !value.is_null() { unsafe { *value = v } } 1 }
                Err(_) => { set_gerror!(error, FWUPD_ERROR_READ, c"read bounds check failed".as_ptr()); 0 }
            }
        }
    };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memread_uint8_safe(
    buf: *const u8,
    bufsz: usize,
    offset: usize,
    value: *mut u8,
    error: *mut *mut GError,
) -> i32 {
    debug_assert!(!buf.is_null());
    if buf.is_null() {
        return 0;
    }
    let data = unsafe { buf_to_slice(buf, bufsz) };
    match fwupd::mem::memread_uint8_safe(data, offset) {
        Ok(v) => {
            if !value.is_null() {
                unsafe { *value = v }
            }
            1
        }
        Err(_) => {
            set_gerror!(error, FWUPD_ERROR_READ, c"read bounds check failed".as_ptr());
            0
        }
    }
}

memread_safe_fn!(fu_memread_uint16_safe, u16, fwupd::mem::memread_uint16_safe);
memread_safe_fn!(fu_memread_uint24_safe, u32, fwupd::mem::memread_uint24_safe);
memread_safe_fn!(fu_memread_uint32_safe, u32, fwupd::mem::memread_uint32_safe);
memread_safe_fn!(fu_memread_uint64_safe, u64, fwupd::mem::memread_uint64_safe);

// -- memwrite_safe --

macro_rules! memwrite_safe_fn {
    ($name:ident, $t:ty, $fn:path) => {
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn $name(
            buf: *mut u8, bufsz: usize, offset: usize,
            value: $t, endian: u32, error: *mut *mut GError,
        ) -> i32 {
            debug_assert!(!buf.is_null());
            if buf.is_null() { return 0 }
            let data = unsafe { buf_to_slice_mut(buf, bufsz) };
            match $fn(data, offset, value, endian_from_c(endian)) {
                Ok(()) => 1,
                Err(_) => { set_gerror!(error, FWUPD_ERROR_WRITE, c"write bounds check failed".as_ptr()); 0 }
            }
        }
    };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_memwrite_uint8_safe(
    buf: *mut u8,
    bufsz: usize,
    offset: usize,
    value: u8,
    error: *mut *mut GError,
) -> i32 {
    debug_assert!(!buf.is_null());
    if buf.is_null() {
        return 0;
    }
    let data = unsafe { buf_to_slice_mut(buf, bufsz) };
    match fwupd::mem::memwrite_uint8_safe(data, offset, value) {
        Ok(()) => 1,
        Err(_) => {
            set_gerror!(error, FWUPD_ERROR_WRITE, c"write bounds check failed".as_ptr());
            0
        }
    }
}

memwrite_safe_fn!(
    fu_memwrite_uint16_safe,
    u16,
    fwupd::mem::memwrite_uint16_safe
);
memwrite_safe_fn!(
    fu_memwrite_uint32_safe,
    u32,
    fwupd::mem::memwrite_uint32_safe
);
memwrite_safe_fn!(
    fu_memwrite_uint64_safe,
    u64,
    fwupd::mem::memwrite_uint64_safe
);
