/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Partial (slice/view) input stream.
//!
//! Provides [`PartialInputStream`], a zero-copy view over a region of another
//! stream. All reads and seeks are translated relative to the slice offset and
//! clamped to the slice bounds.

use crate::streams::{IsSeekable, ReadSeek, StreamError};

use std::io::{self, Read, Seek, SeekFrom};
use std::sync::{Arc, Mutex};

/// A slice/view over another stream.
///
/// The partial input stream does not copy data. It holds a reference to
/// a base stream and translates all seek/read operations by adding the
/// slice offset, clamping reads to the slice bounds.
pub struct PartialInputStream {
    base: Arc<Mutex<dyn ReadSeek + Send>>,
    offset: usize,
    size: usize,
}

impl PartialInputStream {
    /// Create a new partial input stream from a shared stream.
    ///
    /// The stream is a view into `base` starting at `offset` bytes with
    /// length `size` bytes. The base stream is shared via `Arc<Mutex<..>>`
    /// so multiple partial streams can reference the same backing stream.
    ///
    /// <div class="warning">
    ///
    /// This function does not verify that `offset + size` is valid in the
    /// underlying base stream.
    ///
    /// </div>
    pub fn from_stream(base: Arc<Mutex<dyn ReadSeek + Send>>, offset: usize, size: usize) -> Self {
        Self { base, offset, size }
    }

    /// Return the offset of this partial stream within the base stream.
    #[must_use]
    pub fn offset(&self) -> usize {
        self.offset
    }

    /// Return the total size of the partial stream in bytes.
    ///
    /// <div class="warning">
    ///
    /// The returned value is the size provided to [`from_stream`](PartialInputStream::from_stream),
    /// which may be larger than the available size of the underlying base stream.
    ///
    /// </div>
    ///
    /// # Errors
    ///
    /// This method does not currently return an error. The [`Result`] return
    /// type exists for API consistency with fallible stream types.
    pub fn size(&self) -> Result<usize, StreamError> {
        Ok(self.size)
    }
}

impl Read for PartialInputStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let mut base = self
            .base
            .lock()
            .map_err(|e| io::Error::other(e.to_string()))?;
        // Can't use relative_pos here because we need to hold the lock
        let base_pos = base.stream_position()?;
        let tell_pos = base_pos
            .checked_sub(self.offset as u64)
            .ok_or_else(|| io::Error::other("tell returned invalid position"))?;
        let tell_pos = usize::try_from(tell_pos)
            .map_err(|_| io::Error::other("tell position usize overflow"))?;
        if tell_pos >= self.size {
            return Ok(0); // EOF
        }
        let remaining = self.size - tell_pos;
        let count = buf.len().min(remaining);
        if count == 0 {
            return Ok(0);
        }
        base.read(&mut buf[..count])
    }
}

impl Seek for PartialInputStream {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        let mut base = self
            .base
            .lock()
            .map_err(|e| io::Error::other(e.to_string()))?;

        let relative: i64 = match pos {
            SeekFrom::Start(n) => {
                i64::try_from(n).map_err(|_| io::Error::other("invalid seek position"))?
            }
            SeekFrom::Current(n) => {
                let base_pos = base.stream_position()?;
                let base_pos = i64::try_from(base_pos)
                    .map_err(|_| io::Error::other("base position i64 overflow"))?;
                let off = i64::try_from(self.offset)
                    .map_err(|_| io::Error::other("offset i64 overflow"))?;
                let rel = base_pos
                    .checked_sub(off)
                    .ok_or(io::Error::other("base position - offset i64 underflow"))?;

                if rel < 0 {
                    return Err(io::Error::other("tell returned invalid position"));
                }
                rel.checked_add(n)
                    .ok_or_else(|| io::Error::other("size overflow"))?
            }
            SeekFrom::End(n) => {
                let size =
                    i64::try_from(self.size).map_err(|_| io::Error::other("size i64 overflow"))?;
                size.checked_add(n)
                    .ok_or_else(|| io::Error::other("seek position overflow"))?
            }
        };

        if relative < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "seek to negative position",
            ));
        }
        let relative = usize::try_from(relative)
            .map_err(|_| io::Error::other("seek position usize overflow"))?;
        let abs = self
            .offset
            .checked_add(relative)
            .ok_or_else(|| io::Error::other("seek position overflow"))?;

        base.seek(SeekFrom::Start(abs as u64))?;
        Ok(relative as u64)
    }
}

impl IsSeekable for PartialInputStream {
    fn is_seekable(&self) -> bool {
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::streams::MemoryInputStream;

    #[test]
    fn partial_from_rust_stream() {
        let backing = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![
            10, 20, 30, 40, 50, 60, 70, 80,
        ])));
        let mut partial = PartialInputStream::from_stream(backing, 2, 4);

        assert_eq!(partial.size().unwrap(), 4);
        assert_eq!(partial.offset(), 2);

        partial.seek(SeekFrom::Start(0)).unwrap();
        assert_eq!(partial.stream_position().unwrap(), 0);

        let mut buf = [0u8; 10];
        let n = partial.read(&mut buf).unwrap();
        assert_eq!(n, 4);
        assert_eq!(&buf[..4], &[30, 40, 50, 60]);

        // EOF
        assert_eq!(partial.read(&mut buf).unwrap(), 0);
    }

    #[test]
    fn partial_from_rust_stream_seek() {
        let backing = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![
            0, 1, 2, 3, 4, 5, 6, 7,
        ])));
        let mut partial = PartialInputStream::from_stream(backing, 2, 4);

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
        let backing = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![
            0, 1, 2, 3, 4, 5,
        ])));
        let mut partial = PartialInputStream::from_stream(backing, 1, 3);

        partial.seek(SeekFrom::Start(0)).unwrap();
        let mut buf = [0u8; 10];
        let n = partial.read(&mut buf).unwrap();
        assert_eq!(n, 3); // clamped to size
        assert_eq!(&buf[..3], &[1, 2, 3]);
    }
}
