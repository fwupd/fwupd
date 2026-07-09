/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! C-compatible FFI wrappers for the fwupd stream types.
//!
//! All our `FuInputStream` implementations (i.e. the only callers
//! we have into this module) have a Rust implementation,
//! this FFI builds on that knowledge.
//!
//! Our Rust streams are wrapped into an `Arc<Mutex<stream>>`
//! and passed as a raw Box pointer back to the caller. Any future
//! entry point recovers that Arc and calls directly into it.

#[cfg(unix)]
use std::os::unix::ffi::OsStrExt;

use std::ffi::{CStr, OsStr};
use std::io::{Read, Seek, SeekFrom};
#[cfg(unix)]
use std::os::fd::{FromRawFd, OwnedFd};
use std::os::raw::c_char;
use std::ptr;

use std::sync::{Arc, Mutex};

use crate::glib::{
    GBoolean, GError, GOffset, GSeekType, GFALSE, GTRUE, G_SEEK_CUR, G_SEEK_END, G_SEEK_SET,
};
use fwupd::streams::{
    BorrowedMemoryInputStream, CStream, CStreamCallbacks, CStreamHandle, CompositeInputStream,
    FileInputStream, IsSeekable, MemoryInputStream, PartialInputStream, ReadSeek,
};

/// The type of our underlying Rust stream implementation, wrapped
/// into an `Arc<Mutex>` so we can have multiple references to it,
/// e.g. in the case of Partial or Composite streams.
///
/// The `ReadSeek` is implemented by each stream, Send is just
/// there to shut clippy up, we don't really need it since fwupd
/// is single-threaded.
type StreamImpl = Arc<Mutex<dyn ReadSeek + Send>>;

/// EINVAL — same value (22) on Linux, BSD, and macOS.
/// Only ever used as emergency fallback so should be good enough.
const EINVAL: isize = 22;

/// Convert an [`std::io::Error`] to a negative errno value suitable for
/// returning to C. Falls back to `-EINVAL` for non-OS errors.
fn io_error_to_neg_errno(e: &std::io::Error) -> isize {
    if let Some(errno) = e.raw_os_error() {
        -(errno as isize)
    } else {
        -EINVAL
    }
}

/// Convert a `GLib` `GSeekType` i32 value and offset to a [`SeekFrom`].
fn seek_from_glib(seek_type: GSeekType, offset: GOffset) -> Option<SeekFrom> {
    match seek_type {
        G_SEEK_CUR => Some(SeekFrom::Current(offset)),
        G_SEEK_SET => {
            if offset < 0 {
                return None;
            }
            Some(SeekFrom::Start(u64::try_from(offset).unwrap()))
        }
        G_SEEK_END => Some(SeekFrom::End(offset)),
        _ => None,
    }
}

macro_rules! ffi_stream_impl_free {
    ($type:ty, $func:ident) => {
        /// Free the stream.
        ///
        /// # Safety
        /// `stream` must be a valid pointer previously returned by the
        /// corresponding `_new` constructor, or NULL (no-op).
        #[no_mangle]
        pub unsafe extern "C" fn $func(stream: *mut Arc<Mutex<$type>>) {
            if !stream.is_null() {
                drop(unsafe { Box::from_raw(stream) });
            }
        }
    };
}

macro_rules! ffi_stream_impl_read {
    ($type:ty, $func:ident) => {
        /// Read up to `count` bytes into `buf`.
        /// Returns the number of bytes read, or a negative errno on error.
        ///
        /// # Panics
        ///
        /// Panics if the stream mutex is poisoned.
        ///
        /// # Safety
        /// `stream` must be a valid, non-NULL pointer. `buf` must point to
        /// at least `count` writable bytes and must not be NULL if `count > 0`.
        #[no_mangle]
        pub unsafe extern "C" fn $func(
            stream: *mut Arc<Mutex<$type>>,
            buf: *mut u8,
            count: usize,
        ) -> isize {
            if stream.is_null() || buf.is_null() {
                return if count == 0 { 0 } else { -EINVAL };
            }
            let arc = unsafe { &*stream };
            let mut stream = arc.lock().unwrap();
            let slice = unsafe { std::slice::from_raw_parts_mut(buf, count) };
            match stream.read(slice) {
                Ok(n) => isize::try_from(n).unwrap_or(-EINVAL),
                Err(e) => io_error_to_neg_errno(&e),
            }
        }
    };
}

macro_rules! ffi_stream_impl_seek {
    ($type:ty, $func:ident) => {
        /// Seek in the stream.  Returns gboolean (1 = TRUE, 0 = FALSE).
        ///
        /// # Panics
        ///
        /// Panics if the stream mutex is poisoned.
        ///
        /// # Safety
        /// `stream` must be a valid, non-NULL pointer.
        #[no_mangle]
        pub unsafe extern "C" fn $func(
            stream: *mut Arc<Mutex<$type>>,
            offset: GOffset,
            seek_type: GSeekType,
        ) -> GBoolean {
            if stream.is_null() {
                return GFALSE;
            }
            let arc = unsafe { &*stream };
            let mut stream = arc.lock().unwrap();
            let Some(seek_from) = seek_from_glib(seek_type, offset) else {
                return GFALSE;
            };
            if stream.seek(seek_from).is_ok() {
                GTRUE
            } else {
                GFALSE
            }
        }
    };
}

macro_rules! ffi_stream_impl_can_seek {
    ($type:ty, $func:ident) => {
        /// Returns gboolean `GTRUE` if the underlying stream can seek
        ///
        /// # Panics
        ///
        /// Panics if the stream mutex is poisoned.
        ///
        /// # Safety
        /// `stream` must be a valid, non-NULL pointer.
        #[no_mangle]
        pub unsafe extern "C" fn $func(stream: *const Arc<Mutex<$type>>) -> GBoolean {
            if stream.is_null() {
                return GFALSE;
            }
            let arc = unsafe { &*stream };
            let stream = arc.lock().unwrap();
            if stream.is_seekable() {
                GTRUE
            } else {
                GFALSE
            }
        }
    };
}

macro_rules! ffi_stream_impl_tell {
    ($type:ty, $func:ident) => {
        /// Return the current stream position, or 0 on error.
        ///
        /// # Panics
        ///
        /// Panics if the stream mutex is poisoned.
        ///
        /// # Safety
        /// `stream` must be a valid, non-NULL pointer.
        #[no_mangle]
        pub unsafe extern "C" fn $func(stream: *const Arc<Mutex<$type>>) -> GOffset {
            if stream.is_null() {
                return 0;
            }
            let arc = unsafe { &*stream };
            let mut stream = arc.lock().unwrap();
            let pos = stream.stream_position().unwrap_or(0);
            // pos > 2^63 is not worth worrying about
            let pos = i64::try_from(pos).unwrap_or(0);
            pos as GOffset
        }
    };
}

macro_rules! ffi_stream_impl_size {
    ($type:ty, $func:ident) => {
        /// Return the total stream size, or 0 on error.
        ///
        /// # Panics
        ///
        /// Panics if the stream mutex is poisoned.
        ///
        /// # Safety
        /// `stream` must be a valid, non-NULL pointer.
        #[no_mangle]
        pub unsafe extern "C" fn $func(stream: *const Arc<Mutex<$type>>) -> usize {
            if stream.is_null() {
                return 0;
            }
            let arc = unsafe { &*stream };
            let stream = arc.lock().unwrap();
            stream.size().unwrap_or(0)
        }
    };
}

macro_rules! ffi_stream_impl_stream_impl {
    ($type:ty, $func:ident) => {
        /// Return a type-erased `StreamImpl` handle from the given stream.
        ///
        /// The returned handle can be passed to functions that accept any
        /// stream type (e.g. `fu_rs_partial_input_stream_new` or
        /// `fu_rs_composite_input_stream_add_stream`).  The caller must
        /// free it with `fu_stream_impl_free`.
        ///
        /// # Safety
        /// `stream` must be a valid, non-NULL pointer previously returned
        /// by the corresponding `_new` constructor.
        #[no_mangle]
        pub unsafe extern "C" fn $func(stream: *const Arc<Mutex<$type>>) -> *mut StreamImpl {
            if stream.is_null() {
                return ptr::null_mut();
            }
            let arc = unsafe { &*stream };
            let stream_impl: StreamImpl = arc.clone();
            Box::into_raw(Box::new(stream_impl))
        }
    };
}

/// Create a [`BorrowedMemoryInputStream`] from the given
/// data.
///
/// The caller is responsible for keeping data alive for the lifetime of the
/// stream.
///
/// # Safety
/// `data` must be non-NULL point to at least `len` readable bytes, otherwise if
/// data is NULL `len` must be 0.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_borrowed_memory_input_stream_new_from_data(
    data: *const u8,
    len: usize,
) -> *mut Arc<Mutex<BorrowedMemoryInputStream<'static>>> {
    if data.is_null() && len != 0 {
        return ptr::null_mut();
    }
    let data = if len == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    Box::into_raw(Box::new(Arc::new(Mutex::new(
        BorrowedMemoryInputStream::from_data(data),
    ))))
}

ffi_stream_impl_free!(
    BorrowedMemoryInputStream,
    fu_rs_borrowed_memory_input_stream_free
);
ffi_stream_impl_read!(
    BorrowedMemoryInputStream,
    fu_rs_borrowed_memory_input_stream_read
);
ffi_stream_impl_seek!(
    BorrowedMemoryInputStream,
    fu_rs_borrowed_memory_input_stream_seek
);
ffi_stream_impl_can_seek!(
    BorrowedMemoryInputStream,
    fu_rs_borrowed_memory_input_stream_can_seek
);
ffi_stream_impl_tell!(
    BorrowedMemoryInputStream,
    fu_rs_borrowed_memory_input_stream_tell
);
ffi_stream_impl_size!(
    BorrowedMemoryInputStream,
    fu_rs_borrowed_memory_input_stream_size
);

/// Return a type-erased `StreamImpl` handle from the given
/// borrowed memory input stream.
///
/// The returned handle can be passed to functions that accept any
/// stream type.  The caller must free it with `fu_stream_impl_free`.
///
/// # Safety
/// `stream` must be a valid, non-NULL pointer previously returned
/// by [`fu_rs_borrowed_memory_input_stream_new_from_data`].
#[no_mangle]
pub unsafe extern "C" fn fu_rs_borrowed_memory_input_stream_get_stream_impl(
    stream: *const Arc<Mutex<BorrowedMemoryInputStream<'static>>>,
) -> *mut StreamImpl {
    if stream.is_null() {
        return ptr::null_mut();
    }
    let arc = unsafe { &*stream };
    let shared: StreamImpl = arc.clone();
    Box::into_raw(Box::new(shared))
}

/// Create a [`MemoryInputStream`] from a copy
/// of the given data.
///
/// # Safety
/// `data` must be non-NULL point to at least `len` readable bytes, otherwise if
/// data is NULL `len` must be 0.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_memory_input_stream_new_from_data(
    data: *const u8,
    len: usize,
) -> *mut Arc<Mutex<MemoryInputStream>> {
    let vec = if data.is_null() || len == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(data, len) }.to_vec()
    };
    Box::into_raw(Box::new(Arc::new(Mutex::new(
        MemoryInputStream::from_data(vec),
    ))))
}

ffi_stream_impl_free!(MemoryInputStream, fu_rs_memory_input_stream_free);
ffi_stream_impl_read!(MemoryInputStream, fu_rs_memory_input_stream_read);
ffi_stream_impl_seek!(MemoryInputStream, fu_rs_memory_input_stream_seek);
ffi_stream_impl_can_seek!(MemoryInputStream, fu_rs_memory_input_stream_can_seek);
ffi_stream_impl_tell!(MemoryInputStream, fu_rs_memory_input_stream_tell);
ffi_stream_impl_size!(MemoryInputStream, fu_rs_memory_input_stream_size);
ffi_stream_impl_stream_impl!(MemoryInputStream, fu_rs_memory_input_stream_get_stream_impl);

/// Create a [`FileInputStream`] from
/// the given file path. Returns NULL on error.
///
/// # Safety
/// `path` must be a valid NUL-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_file_input_stream_new_from_path(
    path: *const c_char,
    error: *mut *mut GError,
) -> *mut Arc<Mutex<FileInputStream>> {
    if path.is_null() {
        let e = fwupd::Error::new(fwupd::ErrorKind::InvalidData, "file path is NULL");
        GError::convert(error, &e);
        return ptr::null_mut();
    }

    let path_str = unsafe { CStr::from_ptr(path) };

    #[cfg(unix)]
    let os_str = OsStr::from_bytes(path_str.to_bytes());

    #[cfg(windows)]
    let lossy = path_str.to_string_lossy();
    #[cfg(windows)]
    let os_str = OsStr::new(lossy.as_ref());

    match FileInputStream::open(os_str) {
        Ok(stream) => Box::into_raw(Box::new(Arc::new(Mutex::new(stream)))),
        Err(e) => {
            GError::convert(error, &e.into());
            ptr::null_mut()
        }
    }
}

/// Create a [`FileInputStream`] from
/// the given fd. Returns NULL on error.
///
/// # Safety
/// `fd` must be a valid file descriptor. Ownership of fd is transferred,
/// the caller must not close the fd.
#[cfg(unix)]
#[no_mangle]
pub unsafe extern "C" fn fu_rs_file_input_stream_new_from_fd(
    fd: i32,
    error: *mut *mut GError,
) -> *mut Arc<Mutex<FileInputStream>> {
    if fd < 0 {
        let e = fwupd::Error::new(fwupd::ErrorKind::InvalidData, "invalid fd");
        GError::convert(error, &e);
        return ptr::null_mut();
    }
    let owned: OwnedFd = unsafe { OwnedFd::from_raw_fd(fd) };
    let file = std::fs::File::from(owned);
    Box::into_raw(Box::new(Arc::new(Mutex::new(FileInputStream::from_file(
        file,
    )))))
}

/// Return the underlying file descriptor.
///
/// # Panics
///
/// Panics if the stream mutex is poisoned.
///
/// # Safety
/// `stream` must be a valid pointer previously returned by the
/// corresponding `_new` constructor, or NULL (no-op).
#[cfg(unix)]
#[no_mangle]
pub unsafe extern "C" fn fu_rs_file_input_stream_get_fd(
    stream: *const Arc<Mutex<FileInputStream>>,
) -> i32 {
    if stream.is_null() {
        return -1;
    }
    let arc = unsafe { &*stream };
    let stream = arc.lock().unwrap();
    stream.raw_fd()
}

ffi_stream_impl_free!(FileInputStream, fu_rs_file_input_stream_free);
ffi_stream_impl_read!(FileInputStream, fu_rs_file_input_stream_read);
ffi_stream_impl_seek!(FileInputStream, fu_rs_file_input_stream_seek);
ffi_stream_impl_can_seek!(FileInputStream, fu_rs_file_input_stream_can_seek);
ffi_stream_impl_tell!(FileInputStream, fu_rs_file_input_stream_tell);
ffi_stream_impl_size!(FileInputStream, fu_rs_file_input_stream_size);
ffi_stream_impl_stream_impl!(FileInputStream, fu_rs_file_input_stream_get_stream_impl);

/// Create a new [`PartialInputStream`]
/// from the given stream.
///
/// # Safety
/// `base_stream` must be a valid `StreamImpl` handle previously returned
/// by an `_as_shared` function. Ownership is transferred to the new stream.
#[no_mangle]
#[allow(clippy::arc_with_non_send_sync)]
pub unsafe extern "C" fn fu_rs_partial_input_stream_new(
    base_stream: *mut StreamImpl,
    offset: usize,
    size: usize,
) -> *mut Arc<Mutex<PartialInputStream>> {
    if base_stream.is_null() {
        return ptr::null_mut();
    }
    let stream_impl = unsafe { *Box::from_raw(base_stream) };
    let partial = PartialInputStream::from_stream(stream_impl, offset, size);
    Box::into_raw(Box::new(Arc::new(Mutex::new(partial))))
}

ffi_stream_impl_free!(PartialInputStream, fu_rs_partial_input_stream_free);
ffi_stream_impl_read!(PartialInputStream, fu_rs_partial_input_stream_read);
ffi_stream_impl_seek!(PartialInputStream, fu_rs_partial_input_stream_seek);
ffi_stream_impl_can_seek!(PartialInputStream, fu_rs_partial_input_stream_can_seek);
ffi_stream_impl_tell!(PartialInputStream, fu_rs_partial_input_stream_tell);
ffi_stream_impl_size!(PartialInputStream, fu_rs_partial_input_stream_size);
ffi_stream_impl_stream_impl!(
    PartialInputStream,
    fu_rs_partial_input_stream_get_stream_impl
);

/// Return the offset of the partial stream within the base stream.
///
/// # Panics
///
/// Panics if the stream mutex is poisoned.
///
/// # Safety
/// `stream` must be valid.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_partial_input_stream_offset(
    stream: *const Arc<Mutex<PartialInputStream>>,
) -> usize {
    if stream.is_null() {
        return 0;
    }
    let arc = unsafe { &*stream };
    let stream = arc.lock().unwrap();
    stream.offset()
}

/// Create a new empty [`CompositeInputStream`].
#[no_mangle]
pub extern "C" fn fu_rs_composite_input_stream_new() -> *mut Arc<Mutex<CompositeInputStream>> {
    Box::into_raw(Box::new(Arc::new(Mutex::new(CompositeInputStream::new()))))
}

/// Add a sub-stream to the composite.
///
/// # Panics
///
/// Panics if the stream mutex is poisoned.
///
/// # Safety
/// `stream` must be valid. `sub_stream` must be a valid `StreamImpl` handle
/// previously returned by an `_as_shared` function. Ownership of `sub_stream`
/// is transferred.
#[no_mangle]
#[allow(clippy::arc_with_non_send_sync)]
pub unsafe extern "C" fn fu_rs_composite_input_stream_add_stream(
    stream: *mut Arc<Mutex<CompositeInputStream>>,
    sub_stream: *mut StreamImpl,
    size: usize,
) {
    if sub_stream.is_null() {
        return;
    }
    let shared = unsafe { *Box::from_raw(sub_stream) };
    if stream.is_null() {
        return;
    }
    let arc = unsafe { &mut *stream };
    let mut guard = arc.lock().unwrap();
    guard.add_stream(shared, size);
}

ffi_stream_impl_free!(CompositeInputStream, fu_rs_composite_input_stream_free);
ffi_stream_impl_read!(CompositeInputStream, fu_rs_composite_input_stream_read);
ffi_stream_impl_seek!(CompositeInputStream, fu_rs_composite_input_stream_seek);
ffi_stream_impl_can_seek!(CompositeInputStream, fu_rs_composite_input_stream_can_seek);
ffi_stream_impl_tell!(CompositeInputStream, fu_rs_composite_input_stream_tell);
ffi_stream_impl_size!(CompositeInputStream, fu_rs_composite_input_stream_size);
ffi_stream_impl_stream_impl!(
    CompositeInputStream,
    fu_rs_composite_input_stream_get_stream_impl
);

/// Create a new [`CStream`] from the given handle
/// and callbacks.
///
/// This can be used by callers to wrap an arbitrary
/// `GInputStream` into a Rust-backed stream.
///
/// # Safety
/// `handle` must be a valid pointer to a C `GInputStream`.
/// `callbacks` must contain valid function pointers.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cstream_new(
    handle: CStreamHandle,
    callbacks: CStreamCallbacks,
) -> *mut Arc<Mutex<CStream>> {
    if handle.is_null() {
        return ptr::null_mut();
    }
    Box::into_raw(Box::new(Arc::new(Mutex::new(CStream::new(
        handle, callbacks,
    )))))
}

ffi_stream_impl_free!(CStream, fu_rs_cstream_free);
ffi_stream_impl_tell!(CStream, fu_rs_cstream_tell);
ffi_stream_impl_stream_impl!(CStream, fu_rs_cstream_get_stream_impl);
ffi_stream_impl_can_seek!(CStream, fu_rs_cstream_can_seek);
// read, seek and size are different for CStream

/// Read up to `count` bytes into `buf`.
/// Returns the number of bytes read, or a negative errno on error.
///
/// # Panics
///
/// Panics if the stream mutex is poisoned.
///
/// # Safety
/// `stream` must be a valid, non-NULL pointer. `buf` must point to
/// at least `count` writable bytes and must not be NULL if `count > 0`.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cstream_read(
    stream: *mut Arc<Mutex<CStream>>,
    buf: *mut u8,
    count: usize,
    gerror: *mut *mut GError,
) -> isize {
    if stream.is_null() || buf.is_null() {
        return if count == 0 { 0 } else { -EINVAL };
    }
    let arc = unsafe { &mut *stream };
    let mut stream = arc.lock().unwrap();
    let slice = unsafe { std::slice::from_raw_parts_mut(buf, count) };
    stream.read_stream(slice, gerror.cast())
}

/// Seek in the stream.  Returns gboolean (1 = TRUE, 0 = FALSE).
///
/// # Panics
///
/// Panics if the stream mutex is poisoned.
///
/// # Safety
/// `stream` must be a valid, non-NULL pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cstream_seek(
    stream: *mut Arc<Mutex<CStream>>,
    offset: GOffset,
    seek_type: GSeekType,
    gerror: *mut *mut GError,
) -> GBoolean {
    if stream.is_null() {
        return GFALSE;
    }
    let arc = unsafe { &mut *stream };
    let mut stream = arc.lock().unwrap();
    let Some(seek_from) = seek_from_glib(seek_type, offset) else {
        return GFALSE;
    };
    // Note: cstream::GError is a pointer, our glib::GError (the function argument) is a struct
    stream.seek_stream(seek_from, gerror.cast())
}

/// Return the total stream size, or 0 on error.
///
/// # Panics
///
/// Panics if the stream mutex is poisoned.
///
/// # Safety
/// `stream` must be a valid, non-NULL pointer.
#[no_mangle]
pub unsafe extern "C" fn fu_rs_cstream_size(stream: *mut Arc<Mutex<CStream>>) -> usize {
    if stream.is_null() {
        return 0;
    }
    let arc = unsafe { &mut *stream };
    let mut stream = arc.lock().unwrap();
    stream.size().unwrap_or(0)
}

/// Free a shared stream handle.
///
/// # Safety
/// `stream` must be a valid pointer previously returned by an `_get_stream_impl`
/// function, or NULL (no-op).
#[no_mangle]
pub unsafe extern "C" fn fu_stream_impl_free(stream: *mut StreamImpl) {
    if !stream.is_null() {
        drop(unsafe { Box::from_raw(stream) });
    }
}
