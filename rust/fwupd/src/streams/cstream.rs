/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! C-side `GInputStream` interop for fwupd streams.
//!
//! Provides [`CStream`], a wrapper that delegates [`Read`] and [`Seek`]
//! operations to a C-side `GInputStream` through a [`CStreamCallbacks`] vtable.
//! This allows C-backed streams to be used as trait objects alongside native
//! Rust stream types such as [`super::MemoryInputStream`].
//!
//! [`CStream`] has a second API for reading and seeking, see [`CStream::read_stream`]
//! and [`CStream::seek_stream`] that can be used by callers that rely on `GError`
//! return values.

use crate::streams::IsSeekable;
use std::io::{self, Read, Seek, SeekFrom};

/// `GLib` `GBoolean` type (an `i32`, not `bool`)
pub type GBoolean = i32;

/// `GLib` `GSeekType`
pub type GSeekType = i32;

/// `GLib` `GSeekType` constants for C callback interop.
const G_SEEK_CUR: GSeekType = 0;
const G_SEEK_SET: GSeekType = 1;
const G_SEEK_END: GSeekType = 2;

/// Opaque handle to a C-side `GInputStream`.
pub type CStreamHandle = *mut std::ffi::c_void;

/// Opaque handle for a `GError`
pub type GError = *mut std::ffi::c_void;

/// Callback vtable for reading/seeking on a C-side stream.
///
/// Used to construct a [`CStream`] that bridges a C-side `GInputStream` into
/// Rust's [`Read`] + [`Seek`] traits.
///
/// <div class="warning">
///
/// The [`CStreamCallbacks::seek_fn`] return type is `i32` (not `bool`) to match `GLib`'s `gboolean`
/// which is `gint` (4 bytes), not C99 `_Bool` (1 byte).
///
/// </div>
#[repr(C)]
#[derive(Clone, Copy)]
pub struct CStreamCallbacks {
    /// Read up to `count` bytes into `buf`. Returns bytes read, or a
    /// negative errno on error.
    pub read_fn: unsafe extern "C" fn(
        handle: CStreamHandle,
        buf: *mut u8,
        count: usize,
        error: *mut GError,
    ) -> isize,
    /// Seek to `offset` with the given `seek_type`. (`GLib` `GSeekType` as i32).
    ///
    /// <div class="warning">
    ///
    /// `seek_type` is a `GLib` `GSeekType` as [i32], **not** a POSIX `whence` for `fseek(3)`.
    /// Returns `GBoolean` (`i32`): 1 (`GTRUE`) for success, 0 (`GFALSE`) for failure.
    ///
    /// </div>
    ///
    /// Returns nonzero on success or zero on failure.
    pub seek_fn: unsafe extern "C" fn(
        handle: CStreamHandle,
        offset: i64,
        seek_type: GSeekType,
        error: *mut GError,
    ) -> GBoolean,
    /// Return `GTRUE` if the stream is seekable.
    pub can_seek_fn: unsafe extern "C" fn(handle: CStreamHandle) -> GBoolean,
    /// Return the current position in the stream.
    pub tell_fn: unsafe extern "C" fn(handle: CStreamHandle) -> i64,
    /// Called when the stream is no longer needed. May be NULL.
    pub destroy_fn: Option<unsafe extern "C" fn(handle: CStreamHandle)>,
}

/// A wrapper around C-side stream callbacks, implementing [`Read`] and [`Seek`].
///
/// The underlying stream may be of any type (typically a `GInputStream`)
/// as long as it implements the required [`CStreamCallbacks`].
pub struct CStream {
    handle: CStreamHandle,
    callbacks: CStreamCallbacks,
}

impl CStream {
    /// Create a new C-backed stream from a handle and callbacks.
    ///
    /// # Safety
    ///
    /// The `handle` must be valid for every callback in `callbacks`, and must
    /// stay alive for the whole life of the returned `CStream`. If
    /// [`CStreamCallbacks::destroy_fn`] is set, `CStream` releases the handle
    /// on [`Drop`]; otherwise the caller keeps ownership.
    pub unsafe fn new(handle: CStreamHandle, callbacks: CStreamCallbacks) -> Self {
        Self { handle, callbacks }
    }

    /// Return the total size of the stream in bytes.
    ///
    /// Computes the size by seeking to the end and back to the original
    /// position.
    ///
    /// # Errors
    ///
    /// Returns [`StreamError::Seek`](super::StreamError::Seek) if seeking
    /// to the end or back to the original position fails, or if `tell`
    /// returns a negative value.
    pub fn size(&mut self) -> Result<usize, super::StreamError> {
        let pos = unsafe { (self.callbacks.tell_fn)(self.handle) };
        if pos < 0 {
            return Err(super::StreamError::Seek("tell failed".into()));
        }
        let ok =
            unsafe { (self.callbacks.seek_fn)(self.handle, 0, G_SEEK_END, std::ptr::null_mut()) };
        if ok == 0 {
            return Err(super::StreamError::Seek("seek to end failed".into()));
        }
        let end = unsafe { (self.callbacks.tell_fn)(self.handle) };
        let ok =
            unsafe { (self.callbacks.seek_fn)(self.handle, pos, G_SEEK_SET, std::ptr::null_mut()) };
        if ok == 0 {
            return Err(super::StreamError::Seek("seek back failed".into()));
        }
        if end < 0 {
            return Err(super::StreamError::Seek(
                "tell returned negative size".into(),
            ));
        }
        usize::try_from(end)
            .map_err(|_| super::StreamError::InvalidArgs("stream size exceeds usize".into()))
    }

    /// Read into `buf` and return the number of bytes read. On error
    /// return -1 and set `error` (if not NULL).
    ///
    /// The `error` is passed as-is through to the [`CStreamCallbacks::read_fn`].
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn read_stream(&mut self, buf: &mut [u8], error: *mut GError) -> isize {
        unsafe { (self.callbacks.read_fn)(self.handle, buf.as_mut_ptr(), buf.len(), error) }
    }

    /// Seek to the given position and return 1 on success, 0 on failure.
    ///
    /// The `error` is passed as-is through to the [`CStreamCallbacks::seek_fn`].
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn seek_stream(&mut self, pos: SeekFrom, error: *mut GError) -> GBoolean {
        let (offset, seek_type) = match pos {
            SeekFrom::Start(n) => match i64::try_from(n) {
                Ok(offset) => (offset, G_SEEK_SET),
                Err(_) => return 0, // GFALSE
            },
            SeekFrom::Current(n) => (n, G_SEEK_CUR),
            SeekFrom::End(n) => (n, G_SEEK_END),
        };
        #[allow(clippy::not_unsafe_ptr_arg_deref)]
        unsafe {
            (self.callbacks.seek_fn)(self.handle, offset, seek_type, error)
        }
    }
}

impl Read for CStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let rc = unsafe {
            (self.callbacks.read_fn)(
                self.handle,
                buf.as_mut_ptr(),
                buf.len(),
                std::ptr::null_mut(),
            )
        };
        if rc < 0 {
            return Err(io::Error::other(format!(
                "C stream read returned error {rc}"
            )));
        }
        let n = usize::try_from(rc).unwrap();
        if n > buf.len() {
            return Err(io::Error::other(format!(
                "C stream read returned {n} for a buffer of {}",
                buf.len()
            )));
        }
        Ok(n)
    }
}

impl Seek for CStream {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        let (offset, seek_type) = match pos {
            SeekFrom::Start(n) => {
                let offset =
                    i64::try_from(n).map_err(|_| io::Error::other("seek offset overflow"))?;
                (offset, G_SEEK_SET)
            }
            SeekFrom::Current(n) => (n, G_SEEK_CUR),
            SeekFrom::End(n) => (n, G_SEEK_END),
        };
        let ok = unsafe {
            (self.callbacks.seek_fn)(self.handle, offset, seek_type, std::ptr::null_mut())
        };
        if ok == 0 {
            return Err(io::Error::other("C stream seek failed"));
        }
        let pos = unsafe { (self.callbacks.tell_fn)(self.handle) };
        if pos < 0 {
            // FIXME: should have gerror to io::Error conversion here to be more useful
            // but that requires linkage to glib for g_error_free
            return Err(io::Error::other(format!("C stream tell returned {pos}")));
        }
        let pos = u64::try_from(pos).unwrap();
        Ok(pos)
    }
}

impl IsSeekable for CStream {
    fn is_seekable(&self) -> bool {
        unsafe { (self.callbacks.can_seek_fn)(self.handle) != 0 }
    }
}

impl Drop for CStream {
    fn drop(&mut self) {
        if let Some(destroy_fn) = self.callbacks.destroy_fn {
            unsafe { destroy_fn(self.handle) };
        }
    }
}

/// SAFETY: `CStream` wraps a `GObject` handle via a raw pointer, which is not
/// Send. fwupd is single-threaded and `GObjects` are not thread-safe, so this
/// impl is sound for our use case. The Send bound is required because
/// `PartialInputStream` and `CompositeInputStream` store sub-streams as
/// Arc<Mutex<dyn `ReadSeek` + Send>>.
unsafe impl Send for CStream {}

/// Test helpers for C-backed stream tests.
///
/// These are used by [`PartialInputStream`] and [`CompositeInputStream`] tests
/// to create a [`MemoryInputStream`]-backed C stream via callbacks.
#[cfg(test)]
pub(crate) mod test_helpers {
    use super::*;
    use crate::streams::MemoryInputStream;
    use std::io::{Read, Seek};

    pub(crate) struct TestStream {
        pub(crate) inner: MemoryInputStream,
    }

    unsafe extern "C" fn test_read(
        handle: CStreamHandle,
        buf: *mut u8,
        count: usize,
        _gerror: *mut GError,
    ) -> isize {
        let stream = unsafe { &mut *handle.cast::<TestStream>() };
        let slice = unsafe { std::slice::from_raw_parts_mut(buf, count) };
        isize::try_from(stream.inner.read(slice).unwrap_or(0)).unwrap()
    }

    unsafe extern "C" fn test_seek(
        handle: CStreamHandle,
        offset: i64,
        seek_type: GSeekType,
        _gerror: *mut GError,
    ) -> GBoolean {
        let stream = unsafe { &mut *handle.cast::<TestStream>() };
        let seek_from = match seek_type {
            G_SEEK_CUR => SeekFrom::Current(offset),
            G_SEEK_SET => SeekFrom::Start(u64::try_from(offset).unwrap()),
            G_SEEK_END => SeekFrom::End(offset),
            _ => return 0,
        };
        i32::from(stream.inner.seek(seek_from).is_ok())
    }

    unsafe extern "C" fn test_can_seek(_handle: CStreamHandle) -> GBoolean {
        1 // MemoryInputStream is always seekable
    }

    unsafe extern "C" fn test_tell(handle: CStreamHandle) -> i64 {
        let stream = unsafe { &mut *handle.cast::<TestStream>() };
        i64::try_from(stream.inner.stream_position().unwrap_or(0)).unwrap()
    }

    pub(crate) fn test_callbacks() -> CStreamCallbacks {
        CStreamCallbacks {
            read_fn: test_read,
            seek_fn: test_seek,
            can_seek_fn: test_can_seek,
            tell_fn: test_tell,
            destroy_fn: None,
        }
    }
}
