/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! C-compatible FFI wrappers for compression/decompression functions.

use std::io::{Cursor, Read};
use std::ptr;

use crate::glib::GError;
use fwupd::compressor::{Compressor, CompressorFormat};

/// Convert a C format byte to a [`CompressorFormat`].
pub(crate) fn format_from_c(format: i32) -> Option<CompressorFormat> {
    match format {
        0 => Some(CompressorFormat::Raw),
        1 => Some(CompressorFormat::Zlib),
        2 => Some(CompressorFormat::Gzip),
        _ => None,
    }
}

/// Decompress data, allocating the output buffer in Rust.
///
/// On success, returns 0 and sets `*out_buf` and `*out_len` to the decompressed data.
/// The caller must free `*out_buf` with `fu_rs_compressor_free()`.
///
/// On error, returns -1
///
/// # Safety
/// - `in_buf` must point to `in_len` readable bytes (or be NULL if `in_len` is 0).
/// - `out_buf` and `out_len` must be valid pointers.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_compressor_decompress(
    format: i32,
    in_buf: *const u8,
    in_len: usize,
    out_buf: *mut *mut u8,
    out_len: *mut usize,
    error: *mut *mut GError,
) -> i32 {
    // Prevent zip bombs - limit decompression ratio to a factor 1000
    const MAX_DECOMPRESSION_RATIO: usize = 1000;

    let Some(fmt) = format_from_c(format) else {
        let e = fwupd::Error::new(fwupd::ErrorKind::InvalidData, "Invalid compression format");
        GError::convert(error, &e);
        return -1;
    };

    let input = if in_buf.is_null() || in_len == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(in_buf, in_len) }
    };

    if out_buf.is_null() || out_len.is_null() {
        let e = fwupd::Error::new(fwupd::ErrorKind::InvalidData, "NULL output pointer");
        GError::convert(error, &e);
        return -1;
    }

    let max_size = if in_len == 0 {
        None
    } else {
        Some(in_len.saturating_mul(MAX_DECOMPRESSION_RATIO))
    };
    match fwupd::compressor::decompress(fmt, input, max_size) {
        Ok(data) => {
            let len = data.len();
            let boxed = data.into_boxed_slice();
            let ptr = Box::into_raw(boxed).cast::<u8>();
            unsafe {
                *out_buf = ptr;
                *out_len = len;
            }
            0
        }
        Err(e) => {
            GError::convert(error, &e.into());
            unsafe {
                *out_buf = ptr::null_mut();
                *out_len = 0;
            }
            -1
        }
    }
}

/// Compress data, allocating the output buffer in Rust.
///
/// On success, returns 0 and sets `*out_buf` and `*out_len` to the compressed data.
/// The caller must free `*out_buf` with `fu_rs_compressor_free()`.
///
/// On error, returns -1
///
/// # Safety
/// Same requirements as `fu_rs_compressor_decompress`.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_compressor_compress(
    format: i32,
    in_buf: *const u8,
    in_len: usize,
    out_buf: *mut *mut u8,
    out_len: *mut usize,
    error: *mut *mut GError,
) -> i32 {
    let Some(fmt) = format_from_c(format) else {
        let e = fwupd::Error::new(fwupd::ErrorKind::InvalidData, "Invalid compression format");
        GError::convert(error, &e);
        return -1;
    };

    let input = if in_buf.is_null() || in_len == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(in_buf, in_len) }
    };

    if out_buf.is_null() || out_len.is_null() {
        let e = fwupd::Error::new(fwupd::ErrorKind::InvalidData, "NULL output pointer");
        GError::convert(error, &e);
        return -1;
    }

    let source: Box<dyn Read + Send> = Box::new(Cursor::new(input.to_vec()));
    let mut compressor = match Compressor::new(source, fmt) {
        Ok(c) => c,
        Err(e) => {
            GError::convert(error, &e.into());
            unsafe {
                *out_buf = ptr::null_mut();
                *out_len = 0;
            }
            return -1;
        }
    };

    let mut data = Vec::new();
    match compressor.read_to_end(&mut data) {
        Ok(_) => {
            let len = data.len();
            let boxed = data.into_boxed_slice();
            let ptr = Box::into_raw(boxed).cast::<u8>();
            unsafe {
                *out_buf = ptr;
                *out_len = len;
            }
            0
        }
        Err(e) => {
            GError::convert(error, &e.into());
            unsafe {
                *out_buf = ptr::null_mut();
                *out_len = 0;
            }
            -1
        }
    }
}

/// Free a buffer allocated by `fu_rs_compressor_decompress` or `fu_rs_compressor_compress`.
///
/// # Safety
/// `ptr` must have been returned by `fu_rs_compressor_decompress`,
/// `fu_rs_compressor_compress`, or `fu_rs_compressor_deflate_raw`, or be NULL.
/// `len` must match the `out_len` that was returned.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_compressor_free(ptr: *mut u8, len: usize) {
    if !ptr.is_null() {
        drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(ptr, len)) });
    }
}
