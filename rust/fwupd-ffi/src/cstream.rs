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
//! Rust stream types such as [`MemoryInputStream`](fwupd::streams::MemoryInputStream).
//!
//! [`CStream`] has a second API for reading and seeking, see [`CStream::read_stream`]
//! and [`CStream::seek_stream`] that can be used by callers that rely on `GError`
//! return values.

use crate::glib::{GBoolean, GError, GSeekType, G_SEEK_CUR, G_SEEK_END, G_SEEK_SET};
use fwupd::streams::IsSeekable;
use std::io::{self, Read, Seek, SeekFrom};

/// Opaque handle to a C-side `GInputStream`.
pub type CStreamHandle = *mut std::ffi::c_void;

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
        error: *mut *mut GError,
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
        error: *mut *mut GError,
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
    /// Returns [`StreamError::Seek`](fwupd::streams::StreamError::Seek) if seeking
    /// to the end or back to the original position fails, or if `tell`
    /// returns a negative value.
    pub fn size(&mut self) -> Result<usize, fwupd::streams::StreamError> {
        let pos = unsafe { (self.callbacks.tell_fn)(self.handle) };
        if pos < 0 {
            return Err(fwupd::streams::StreamError::Seek("tell failed".into()));
        }
        let ok =
            unsafe { (self.callbacks.seek_fn)(self.handle, 0, G_SEEK_END, std::ptr::null_mut()) };
        if ok == 0 {
            return Err(fwupd::streams::StreamError::Seek(
                "seek to end failed".into(),
            ));
        }
        let end = unsafe { (self.callbacks.tell_fn)(self.handle) };
        let ok =
            unsafe { (self.callbacks.seek_fn)(self.handle, pos, G_SEEK_SET, std::ptr::null_mut()) };
        if ok == 0 {
            return Err(fwupd::streams::StreamError::Seek("seek back failed".into()));
        }
        if end < 0 {
            return Err(fwupd::streams::StreamError::Seek(
                "tell returned negative size".into(),
            ));
        }
        usize::try_from(end).map_err(|_| {
            fwupd::streams::StreamError::InvalidArgs("stream size exceeds usize".into())
        })
    }

    /// Read into `buf` and return the number of bytes read. On error
    /// return -1 and set `error` (if not NULL).
    ///
    /// The `error` is passed as-is through to the [`CStreamCallbacks::read_fn`].
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn read_stream(&mut self, buf: &mut [u8], error: *mut *mut GError) -> isize {
        unsafe { (self.callbacks.read_fn)(self.handle, buf.as_mut_ptr(), buf.len(), error) }
    }

    /// Seek to the given position and return 1 on success, 0 on failure.
    ///
    /// The `error` is passed as-is through to the [`CStreamCallbacks::seek_fn`].
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn seek_stream(&mut self, pos: SeekFrom, error: *mut *mut GError) -> GBoolean {
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

#[cfg(test)]
pub(crate) mod test_helpers {
    use super::*;
    use fwupd::streams::MemoryInputStream;
    use std::io::{Read, Seek};
    use std::sync::{Arc, Mutex};

    pub(crate) struct TestStream {
        pub(crate) inner: MemoryInputStream,
    }

    unsafe extern "C" fn test_read(
        handle: CStreamHandle,
        buf: *mut u8,
        count: usize,
        _gerror: *mut *mut GError,
    ) -> isize {
        let stream = unsafe { &mut *handle.cast::<TestStream>() };
        let slice = unsafe { std::slice::from_raw_parts_mut(buf, count) };
        isize::try_from(stream.inner.read(slice).unwrap_or(0)).unwrap()
    }

    unsafe extern "C" fn test_seek(
        handle: CStreamHandle,
        offset: i64,
        seek_type: GSeekType,
        _gerror: *mut *mut GError,
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

    /// Helper: create an `Arc<Mutex<CStream>>` from a `TestStream`.
    #[allow(clippy::arc_with_non_send_sync)]
    pub(crate) fn cstream_from(backing: &mut TestStream) -> Arc<Mutex<CStream>> {
        let handle = backing as *mut TestStream as CStreamHandle;
        Arc::new(Mutex::new(unsafe {
            CStream::new(handle, test_callbacks())
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::test_helpers::*;
    use super::*;
    use fwupd::streams::{CompositeInputStream, MemoryInputStream, PartialInputStream};

    #[test]
    fn partial_basic() {
        let mut backing = TestStream {
            inner: MemoryInputStream::from_data(vec![10, 20, 30, 40, 50, 60, 70, 80]),
        };
        let mut partial = PartialInputStream::from_stream(cstream_from(&mut backing), 2, 4);

        assert_eq!(partial.size().unwrap(), 4);
        assert_eq!(partial.offset(), 2);

        partial.seek(SeekFrom::Start(0)).unwrap();
        assert_eq!(partial.stream_position().unwrap(), 0);

        let mut buf = [0u8; 10];
        let n = partial.read(&mut buf).unwrap();
        assert_eq!(n, 4);
        assert_eq!(&buf[..4], &[30, 40, 50, 60]);

        // Should be at EOF now
        assert_eq!(partial.read(&mut buf).unwrap(), 0);
    }

    #[test]
    fn partial_seek() {
        let mut backing = TestStream {
            inner: MemoryInputStream::from_data(vec![0, 1, 2, 3, 4, 5, 6, 7]),
        };
        let mut partial = PartialInputStream::from_stream(cstream_from(&mut backing), 2, 4);

        // SeekFrom::Start
        partial.seek(SeekFrom::Start(1)).unwrap();
        assert_eq!(partial.stream_position().unwrap(), 1);
        let mut buf = [0u8; 1];
        partial.read_exact(&mut buf).unwrap();
        assert_eq!(buf[0], 3); // offset 2 + 1 = index 3 in backing

        // SeekFrom::End
        partial.seek(SeekFrom::End(-1)).unwrap();
        assert_eq!(partial.stream_position().unwrap(), 3);
        partial.read_exact(&mut buf).unwrap();
        assert_eq!(buf[0], 5); // offset 2 + 3 = index 5

        // SeekFrom::Current
        partial.seek(SeekFrom::Start(0)).unwrap();
        partial.seek(SeekFrom::Current(2)).unwrap();
        assert_eq!(partial.stream_position().unwrap(), 2);
    }

    #[test]
    fn partial_clamps_read() {
        let mut backing = TestStream {
            inner: MemoryInputStream::from_data(vec![0, 1, 2, 3, 4, 5]),
        };
        let mut partial = PartialInputStream::from_stream(cstream_from(&mut backing), 1, 3);

        partial.seek(SeekFrom::Start(0)).unwrap();
        let mut buf = [0u8; 10];
        let n = partial.read(&mut buf).unwrap();
        assert_eq!(n, 3); // clamped to size
        assert_eq!(&buf[..3], &[1, 2, 3]);
    }

    #[test]
    fn composite_single_stream() {
        let mut backing = TestStream {
            inner: MemoryInputStream::from_data(vec![1, 2, 3]),
        };
        let mut composite = CompositeInputStream::new();
        composite.add_stream(cstream_from(&mut backing), 3);

        assert_eq!(composite.size().unwrap(), 3);

        let mut buf = [0u8; 10];
        let n = composite.read(&mut buf).unwrap();
        assert_eq!(n, 3);
        assert_eq!(&buf[..3], &[1, 2, 3]);

        // EOF
        assert_eq!(composite.read(&mut buf).unwrap(), 0);
    }

    #[test]
    fn composite_multiple_streams() {
        let mut backing1 = TestStream {
            inner: MemoryInputStream::from_data(vec![1, 2, 3]),
        };
        let mut backing2 = TestStream {
            inner: MemoryInputStream::from_data(vec![4, 5]),
        };
        let mut backing3 = TestStream {
            inner: MemoryInputStream::from_data(vec![6, 7, 8, 9]),
        };

        let mut composite = CompositeInputStream::new();
        composite.add_stream(cstream_from(&mut backing1), 3);
        composite.add_stream(cstream_from(&mut backing2), 2);
        composite.add_stream(cstream_from(&mut backing3), 4);

        assert_eq!(composite.size().unwrap(), 9);

        let mut result = Vec::new();
        let mut buf = [0u8; 4];
        loop {
            let n = composite.read(&mut buf).unwrap();
            if n == 0 {
                break;
            }
            result.extend_from_slice(&buf[..n]);
        }
        assert_eq!(result, vec![1, 2, 3, 4, 5, 6, 7, 8, 9]);
    }

    #[test]
    fn composite_seek() {
        let mut backing1 = TestStream {
            inner: MemoryInputStream::from_data(vec![10, 20, 30]),
        };
        let mut backing2 = TestStream {
            inner: MemoryInputStream::from_data(vec![40, 50, 60]),
        };

        let mut composite = CompositeInputStream::new();
        composite.add_stream(cstream_from(&mut backing1), 3);
        composite.add_stream(cstream_from(&mut backing2), 3);

        // Seek into second stream
        composite.seek(SeekFrom::Start(4)).unwrap();
        assert_eq!(composite.stream_position().unwrap(), 4);

        let mut buf = [0u8; 1];
        composite.read_exact(&mut buf).unwrap();
        assert_eq!(buf[0], 50);

        // Seek from end
        composite.seek(SeekFrom::End(-1)).unwrap();
        assert_eq!(composite.stream_position().unwrap(), 5);
        composite.read_exact(&mut buf).unwrap();
        assert_eq!(buf[0], 60);

        // Seek back to start
        composite.seek(SeekFrom::Start(0)).unwrap();
        composite.read_exact(&mut buf).unwrap();
        assert_eq!(buf[0], 10);
    }
}
