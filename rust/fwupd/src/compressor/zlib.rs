/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Raw zlib FFI bindings.
//!
//! These call directly into the system zlib that fwupd already links against.
//! The `#[link(name = "z")]` attribute ensures `cargo test` can also find
//! the library without a `build.rs`.

use std::ffi::{c_char, c_int, c_uint};
use std::marker::PhantomPinned;
use std::mem::size_of;
use std::os::raw::c_ulong;
use std::pin::Pin;

use crate::compressor::{CompressorFormat, Error};

const Z_OK: c_int = 0;
const Z_STREAM_END: c_int = 1;
const Z_DEFAULT_STRATEGY: c_int = 0;
const Z_DEFLATED: c_int = 8;
const MAX_MEM_LEVEL: c_int = 8;

/// Window bits for raw deflate (no header).
const WINDOW_BITS_RAW: c_int = -15;
/// Window bits for zlib format.
const WINDOW_BITS_ZLIB: c_int = 15;
/// Window bits for gzip format (15 + 16).
const WINDOW_BITS_GZIP: c_int = 31;
/// Window bits for auto-detect zlib or gzip (15 + 32).
const WINDOW_BITS_GZIP_OR_ZLIB: c_int = 47;

/// Compile-time zlib version that matches the [`ZStream`] layout below.
///
/// This is passed to `deflateInit2_`/`inflateInit2_` so zlib can verify that
/// the caller's struct layout is ABI-compatible with the linked library.
/// zlib compares the first character (major version) and returns
/// `Z_VERSION_ERROR` on mismatch.
///
/// This must be updated whenever the [`ZStream`] layout is updated to match
/// a new zlib version.
pub const ZLIB_VERSION: &[u8] = b"1.3.1\0";

/// Raw type used only for FFI pointer casts.
#[repr(C)]
pub struct ZStreamRaw {
    next_in: *const u8,
    avail_in: c_uint,
    total_in: c_ulong,

    next_out: *mut u8,
    avail_out: c_uint,
    total_out: c_ulong,

    msg: *const c_char,
    state: *mut u8, // internal state, opaque

    zalloc: Option<unsafe extern "C" fn()>, // NULL = use default
    zfree: Option<unsafe extern "C" fn()>,  // NULL = use default
    opaque: *mut std::ffi::c_void,          // NULL = use default

    data_type: c_int,
    adler: c_ulong,
    _reserved: c_ulong,
}

#[link(name = "z")]
extern "C" {
    fn deflateInit2_(
        strm: *mut ZStreamRaw,
        level: c_int,
        method: c_int,
        window_bits: c_int,
        mem_level: c_int,
        strategy: c_int,
        version: *const c_char,
        stream_size: c_int,
    ) -> c_int;

    #[link_name = "deflate"]
    fn zlib_deflate(strm: *mut ZStreamRaw, flush: c_int) -> c_int;
    fn deflateEnd(strm: *mut ZStreamRaw) -> c_int;

    fn inflateInit2_(
        strm: *mut ZStreamRaw,
        window_bits: c_int,
        version: *const c_char,
        stream_size: c_int,
    ) -> c_int;

    #[link_name = "inflate"]
    fn zlib_inflate(strm: *mut ZStreamRaw, flush: c_int) -> c_int;
    fn inflateEnd(strm: *mut ZStreamRaw) -> c_int;
}

#[repr(i32)]
pub enum DeflateFlag {
    NoFlush = 0, // Z_NO_FLUSH
    Finish = 4,  // Z_FINISH
}

#[repr(i32)]
pub enum InflateFlag {
    NoFlush = 0, // Z_NO_FLUSH
}

/// Sealed trait implemented by direction marker types.
///
/// The `end_stream` method is called by `Drop` to release zlib resources.
pub trait Direction {
    /// # Safety
    /// `strm` must point to a validly initialized zlib stream of the
    /// matching direction (inflate or deflate).
    unsafe fn end_stream(strm: *mut ZStreamRaw);
}

/// Marker type for an inflate (decompression) stream.
pub enum Inflate {}

impl Direction for Inflate {
    unsafe fn end_stream(strm: *mut ZStreamRaw) {
        let _ = inflateEnd(strm);
    }
}

/// Marker type for a deflate (compression) stream.
pub enum Deflate {}

impl Direction for Deflate {
    unsafe fn end_stream(strm: *mut ZStreamRaw) {
        let _ = deflateEnd(strm);
    }
}

/// Low-level zlib stream structure matching the C `z_stream` layout.
#[repr(C)]
pub struct ZStream<Dir: Direction> {
    raw: ZStreamRaw,
    // --- not part of the C struct (zero-sized markers) ---
    _dir: std::marker::PhantomData<Dir>,
    /// zlib-ng stores a back-pointer to the `z_stream` inside its internal
    /// state, so the struct must not be moved after initialization.
    /// `PhantomPinned` opts out of `Unpin` so that `Pin<Box<ZStream>>`
    /// actually prevents moves.
    _pin: PhantomPinned,
}

impl Default for ZStream<Inflate> {
    fn default() -> Self {
        let raw = ZStreamRaw {
            next_in: std::ptr::null(),
            avail_in: 0,
            total_in: 0,

            next_out: std::ptr::null_mut(),
            avail_out: 0,
            total_out: 0,

            msg: std::ptr::null(),
            state: std::ptr::null_mut(),

            zalloc: None,
            zfree: None,
            opaque: std::ptr::null_mut(),

            data_type: 0,
            adler: 0,
            _reserved: 0,
        };
        ZStream {
            raw,
            _dir: std::marker::PhantomData,
            _pin: PhantomPinned,
        }
    }
}

impl Default for ZStream<Deflate> {
    fn default() -> Self {
        let raw = ZStreamRaw {
            next_in: std::ptr::null(),
            avail_in: 0,
            total_in: 0,

            next_out: std::ptr::null_mut(),
            avail_out: 0,
            total_out: 0,

            msg: std::ptr::null(),
            state: std::ptr::null_mut(),

            zalloc: None,
            zfree: None,
            opaque: std::ptr::null_mut(),

            data_type: 0,
            adler: 0,
            _reserved: 0,
        };
        ZStream {
            raw,
            _dir: std::marker::PhantomData,
            _pin: PhantomPinned,
        }
    }
}

impl<Dir: Direction> ZStream<Dir> {
    /// Get a mutable reference from a pinned pointer.
    ///
    /// # Safety
    /// The caller must not use the returned reference to move the `ZStream`
    /// out of its current allocation.  All methods in this module satisfy
    /// that requirement: they only read/write individual fields or pass
    /// `self` as a raw pointer to zlib FFI functions.
    unsafe fn pinned_mut(self: Pin<&mut Self>) -> &mut Self {
        self.get_unchecked_mut()
    }

    /// Set the input data for the next inflate/deflate call.
    ///
    /// `avail_in` is set to the length of `data`, clamped to `u32::MAX`.
    /// Input data larger than `u32::MAX` requires multiple calls.
    pub fn set_next_in(self: Pin<&mut Self>, data: &[u8]) {
        // SAFETY: we only write to individual fields, no move.
        let this = unsafe { self.pinned_mut() };
        this.raw.next_in = data.as_ptr();
        this.raw.avail_in = u32::try_from(data.len()).unwrap_or(u32::MAX);
    }

    /// Set the output buffer for the next inflate/deflate call.
    ///
    /// `avail_out` is set to the length of `buf`, clamped to `u32::MAX`.
    pub fn set_next_out(self: Pin<&mut Self>, buf: &mut [u8]) {
        // SAFETY: we only write to individual fields, no move.
        let this = unsafe { self.pinned_mut() };
        this.raw.next_out = buf.as_mut_ptr();
        this.raw.avail_out = u32::try_from(buf.len()).unwrap_or(u32::MAX);
    }

    /// Number of input bytes remaining after the last inflate/deflate call.
    pub fn avail_in(&self) -> usize {
        self.raw.avail_in as usize
    }

    /// Number of output bytes remaining after the last inflate/deflate call.
    pub fn avail_out(&self) -> usize {
        self.raw.avail_out as usize
    }
}

const _: () = assert!(size_of::<ZStream<Inflate>>() == size_of::<ZStreamRaw>());
const _: () = assert!(size_of::<ZStream<Deflate>>() == size_of::<ZStreamRaw>());

impl ZStream<Inflate> {
    /// Create a new inflate stream.
    ///
    /// Returns `Pin<Box<Self>>` because zlib-ng stores a back-pointer to
    /// the `z_stream` inside its internal state.  A heap allocation gives
    /// the struct a stable address, and `Pin` prevents it from being moved
    /// afterwards; a plain stack return would let Rust move the struct
    /// after `inflateInit2_`, invalidating that back-pointer and causing
    /// `Z_STREAM_ERROR` on the next `inflate` call.
    pub fn inflator(format: CompressorFormat) -> Result<Pin<Box<ZStream<Inflate>>>, Error> {
        let mut stream = Box::pin(ZStream::<Inflate>::default());
        // SAFETY: init() only passes `self` as a pointer to zlib; it does
        // not move the struct.
        unsafe { stream.as_mut().get_unchecked_mut() }.init(format)?;
        Ok(stream)
    }

    fn init(&mut self, format: CompressorFormat) -> Result<(), Error> {
        let window_bits = match format {
            CompressorFormat::Raw => WINDOW_BITS_RAW,
            CompressorFormat::Zlib => WINDOW_BITS_ZLIB,
            CompressorFormat::Gzip => WINDOW_BITS_GZIP_OR_ZLIB,
        };
        unsafe {
            match inflateInit2_(
                (self as *mut Self).cast::<ZStreamRaw>(),
                window_bits,
                ZLIB_VERSION.as_ptr().cast(),
                c_int::try_from(size_of::<Self>()).unwrap(),
            ) {
                Z_OK => Ok(()),
                Z_STREAM_END => Err(Error::StreamEnd),
                e => Err(Error::Zlib(format!("inflateInit2 failed with {e}"))),
            }
        }
    }

    pub fn inflate(self: Pin<&mut Self>, flags: InflateFlag) -> Result<(), Error> {
        // SAFETY: we only pass self as a pointer to zlib; no move.
        let this = unsafe { self.pinned_mut() };
        unsafe {
            match zlib_inflate((this as *mut Self).cast::<ZStreamRaw>(), flags as i32) {
                Z_OK => Ok(()),
                Z_STREAM_END => Err(Error::StreamEnd),
                e => Err(Error::Zlib(format!("inflate failed with {e}"))),
            }
        }
    }
}

impl ZStream<Deflate> {
    /// Create a new deflate stream.  See [`ZStream::inflator`] for why
    /// this returns a `Pin<Box<…>>`.
    pub fn deflator(
        level: i32,
        format: CompressorFormat,
    ) -> Result<Pin<Box<ZStream<Deflate>>>, Error> {
        let mut stream = Box::pin(ZStream::<Deflate>::default());
        // SAFETY: init() only passes `self` as a pointer to zlib; it does
        // not move the struct.
        unsafe { stream.as_mut().get_unchecked_mut() }.init(level, format)?;
        Ok(stream)
    }

    fn init(&mut self, level: c_int, format: CompressorFormat) -> Result<(), Error> {
        let window_bits = match format {
            CompressorFormat::Raw => WINDOW_BITS_RAW,
            CompressorFormat::Zlib => WINDOW_BITS_ZLIB,
            CompressorFormat::Gzip => WINDOW_BITS_GZIP,
        };
        unsafe {
            match deflateInit2_(
                (self as *mut Self).cast::<ZStreamRaw>(),
                level,
                Z_DEFLATED,
                window_bits,
                MAX_MEM_LEVEL,
                Z_DEFAULT_STRATEGY,
                ZLIB_VERSION.as_ptr().cast(),
                c_int::try_from(size_of::<Self>()).unwrap(),
            ) {
                Z_OK => Ok(()),
                Z_STREAM_END => Err(Error::StreamEnd),
                e => Err(Error::Zlib(format!("deflateInit2 failed with {e}"))),
            }
        }
    }

    pub fn deflate(self: Pin<&mut Self>, flags: DeflateFlag) -> Result<(), Error> {
        // SAFETY: we only pass self as a pointer to zlib; no move.
        let this = unsafe { self.pinned_mut() };
        unsafe {
            match zlib_deflate((this as *mut Self).cast::<ZStreamRaw>(), flags as i32) {
                Z_OK => Ok(()),
                Z_STREAM_END => Err(Error::StreamEnd),
                e => Err(Error::Zlib(format!("deflate failed with {e}"))),
            }
        }
    }
}

impl<Dir: Direction> Drop for ZStream<Dir> {
    fn drop(&mut self) {
        unsafe {
            Dir::end_stream((self as *mut Self).cast::<ZStreamRaw>());
        }
    }
}

// SAFETY: the ZStream struct is thread safe and it's pinned and
// only ever accessed through &mut self. The pointer values in
// the struct are used within a single inflate/deflate only.
unsafe impl<Dir: Direction> Send for ZStream<Dir> {}
