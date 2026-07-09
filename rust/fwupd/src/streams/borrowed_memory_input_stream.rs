/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! In-memory input stream for data owned by the caller.
//!
//! Provides [`BorrowedMemoryInputStream`], an input stream backed by a
//! slice `[u8]` byte buffer **that is owned by the caller**.
//!
//! See [`MemoryInputStream`](crate::streams::MemoryInputStream) for a stream that owns the data.

use crate::streams::{IsSeekable, StreamError};

use std::io::{self, Read, Seek, SeekFrom};

/// In-memory input stream for data owned by the caller.
///
/// See [`MemoryInputStream`](crate::streams::MemoryInputStream)
/// for a stream that owns the data.
pub struct BorrowedMemoryInputStream<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> BorrowedMemoryInputStream<'a> {
    /// Create a memory input stream from existing data.
    #[must_use]
    pub fn from_data(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }

    /// Return the total size of the stream in bytes.
    ///
    /// # Errors
    ///
    /// This method does not currently return an error. The [`Result`] return
    /// type exists for API consistency with fallible stream types.
    pub fn size(&self) -> Result<usize, StreamError> {
        Ok(self.data.len())
    }
}

impl IsSeekable for BorrowedMemoryInputStream<'_> {
    fn is_seekable(&self) -> bool {
        true
    }
}

impl Read for BorrowedMemoryInputStream<'_> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let available = self.data.len().saturating_sub(self.pos);
        let count = buf.len().min(available);
        if count > 0 {
            buf[..count].copy_from_slice(&self.data[self.pos..self.pos + count]);
            self.pos += count;
        }
        Ok(count)
    }
}

impl Seek for BorrowedMemoryInputStream<'_> {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        let new_pos = match pos {
            SeekFrom::Start(n) => i64::try_from(n).ok(),
            SeekFrom::Current(n) => i64::try_from(self.pos).ok().and_then(|p| p.checked_add(n)),
            SeekFrom::End(n) => i64::try_from(self.data.len())
                .ok()
                .and_then(|len| len.checked_add(n)),
        }
        .ok_or_else(|| std::io::Error::from(std::io::ErrorKind::InvalidInput))?;
        let len =
            i64::try_from(self.data.len()).map_err(|_| io::Error::other("data length overflow"))?;
        if new_pos < 0 || new_pos > len {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("seek to {new_pos} out of range [0, {}]", self.data.len()),
            ));
        }
        self.pos = usize::try_from(new_pos).map_err(|_| io::Error::other("usize overflow"))?;
        Ok(self.pos as u64)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn memory_empty() {
        let data = Vec::new();
        let mut stream = BorrowedMemoryInputStream::from_data(&data);
        assert_eq!(stream.size().unwrap(), 0);
        assert_eq!(stream.stream_position().unwrap(), 0);
        let mut buf = [0u8; 10];
        assert_eq!(stream.read(&mut buf).unwrap(), 0);
    }

    #[test]
    fn memory_read_all() {
        let data = vec![1, 2, 3, 4, 5];
        let mut stream = BorrowedMemoryInputStream::from_data(&data);
        assert_eq!(stream.size().unwrap(), 5);

        let mut buf = [0u8; 10];
        assert_eq!(stream.read(&mut buf).unwrap(), 5);
        assert_eq!(&buf[..5], &[1, 2, 3, 4, 5]);
        assert_eq!(stream.stream_position().unwrap(), 5);

        // Read at EOF
        assert_eq!(stream.read(&mut buf).unwrap(), 0);
    }

    #[test]
    fn memory_read_partial() {
        let data = vec![10, 20, 30, 40, 50];
        let mut stream = BorrowedMemoryInputStream::from_data(&data);

        let mut buf = [0u8; 3];
        assert_eq!(stream.read(&mut buf).unwrap(), 3);
        assert_eq!(&buf, &[10, 20, 30]);
        assert_eq!(stream.stream_position().unwrap(), 3);

        assert_eq!(stream.read(&mut buf).unwrap(), 2);
        assert_eq!(&buf[..2], &[40, 50]);
        assert_eq!(stream.stream_position().unwrap(), 5);
    }

    #[test]
    fn memory_seek() {
        let data = vec![1, 2, 3, 4, 5];
        let mut stream = BorrowedMemoryInputStream::from_data(&data);

        // SeekFrom::Start
        stream.seek(SeekFrom::Start(3)).unwrap();
        assert_eq!(stream.stream_position().unwrap(), 3);
        let mut buf = [0u8; 1];
        assert_eq!(stream.read(&mut buf).unwrap(), 1);
        assert_eq!(buf[0], 4);

        // SeekFrom::Current
        stream.seek(SeekFrom::Current(-2)).unwrap();
        assert_eq!(stream.stream_position().unwrap(), 2);

        // SeekFrom::End
        stream.seek(SeekFrom::End(-1)).unwrap();
        assert_eq!(stream.stream_position().unwrap(), 4);
        assert_eq!(stream.read(&mut buf).unwrap(), 1);
        assert_eq!(buf[0], 5);

        // Invalid seek
        assert!(stream.seek(SeekFrom::End(1)).is_err());
    }
}
