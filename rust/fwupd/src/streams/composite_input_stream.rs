/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Composite (concatenating) input stream.
//!
//! Provides [`CompositeInputStream`], which concatenates multiple sub-streams
//! into a single logical stream. Reads are dispatched to the appropriate
//! sub-stream based on the current position.

use crate::streams::{IsSeekable, ReadSeek, StreamError};

use std::io::{self, Read, Seek, SeekFrom};
use std::sync::{Arc, Mutex};

/// A sub-stream within a composite input stream.
struct SubStream {
    stream: Arc<Mutex<dyn ReadSeek + Send>>,
    global_offset: usize,
    size: usize,
}

/// A concatenation of multiple streams.
///
/// The composite input stream maintains its own position counter and
/// dispatches reads to the appropriate sub-stream. Sub-streams can be
/// any type implementing [`ReadSeek`]. Streams are shared via
/// `Arc<Mutex<..>>` so the caller can retain access to them.
pub struct CompositeInputStream {
    items: Vec<SubStream>,
    pos: usize,
    total_size: usize,
    last_item_idx: Option<usize>,
}

impl CompositeInputStream {
    /// Create a new empty composite input stream.
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: Vec::new(),
            pos: 0,
            total_size: 0,
            last_item_idx: None,
        }
    }

    /// Add a shared stream to the composite.
    ///
    /// <div class="warning">
    ///
    /// This function does not verify that the given `size` is valid
    /// in the added stream.
    ///
    /// </div>
    pub fn add_stream(&mut self, stream: Arc<Mutex<dyn ReadSeek + Send>>, size: usize) {
        let global_offset = self.total_size;
        self.items.push(SubStream {
            stream,
            global_offset,
            size,
        });
        self.total_size = self.total_size.saturating_add(size);
    }

    /// Find the sub-stream index that contains the given position.
    fn find_item_for_pos(&self, pos: usize) -> Option<usize> {
        for (i, item) in self.items.iter().enumerate() {
            let item_end = item.global_offset.saturating_add(item.size);
            if pos < item_end {
                return Some(i);
            }
        }
        None
    }

    /// Return the total size of all sub-streams combined.
    ///
    /// <div class="warning">
    ///
    /// The returned value is the combined size of all
    /// substreams as given in [`add_stream()`](CompositeInputStream::add_stream).
    /// This size may be larger than the actual size of all base streams.
    ///
    /// </div>
    ///
    /// # Errors
    ///
    /// This method does not currently return an error. The [`Result`] return
    /// type exists for API consistency with fallible stream types.
    pub fn size(&self) -> Result<usize, StreamError> {
        Ok(self.total_size)
    }
}

impl Default for CompositeInputStream {
    fn default() -> Self {
        Self::new()
    }
}

impl IsSeekable for CompositeInputStream {
    fn is_seekable(&self) -> bool {
        true
    }
}

impl Read for CompositeInputStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if buf.is_empty() || self.pos >= self.total_size {
            return Ok(0);
        }

        // Find which item contains self.pos
        let Some(item_idx) = self.find_item_for_pos(self.pos) else {
            return Ok(0); // past end
        };

        let item = &self.items[item_idx];
        let mut stream = item
            .stream
            .lock()
            .map_err(|e| io::Error::other(e.to_string()))?;

        // If we switched items (or first read), seek within the new item
        if self.last_item_idx != Some(item_idx) {
            let offset_within_item = self
                .pos
                .checked_sub(item.global_offset)
                .ok_or_else(|| io::Error::other("position before item start"))?;
            stream.seek(SeekFrom::Start(offset_within_item as u64))?;
            self.last_item_idx = Some(item_idx);
        }

        // Clamp read to remaining bytes in this item
        let remaining_in_item = item
            .global_offset
            .saturating_add(item.size)
            .saturating_sub(self.pos);
        let count = buf.len().min(remaining_in_item);
        if count == 0 {
            return Ok(0);
        }

        let bytes_read = stream.read(&mut buf[..count])?;
        if bytes_read == 0 {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                format!(
                    "sub-stream {item_idx} is shorter than its declared size ({})",
                    item.size
                ),
            ));
        }
        self.pos += bytes_read;
        Ok(bytes_read)
    }
}

impl Seek for CompositeInputStream {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        let new_pos = match pos {
            SeekFrom::Start(n) => i64::try_from(n).ok(),
            SeekFrom::Current(n) => i64::try_from(self.pos).ok().and_then(|p| p.checked_add(n)),
            SeekFrom::End(n) => i64::try_from(self.total_size)
                .ok()
                .and_then(|len| len.checked_add(n)),
        }
        .ok_or_else(|| std::io::Error::from(std::io::ErrorKind::InvalidInput))?;
        let total =
            i64::try_from(self.total_size).map_err(|_| io::Error::other("total_size overflow"))?;
        if new_pos < 0 || new_pos > total {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("seek to {new_pos} out of range [0, {}]", self.total_size),
            ));
        }
        self.pos = usize::try_from(new_pos).map_err(|_| io::Error::other("usize overflow"))?;
        self.last_item_idx = None;
        Ok(self.pos as u64)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::streams::cstream::test_helpers::{test_callbacks, TestStream};
    use crate::streams::cstream::{CStream, CStreamHandle};
    use crate::streams::MemoryInputStream;
    use std::io::ErrorKind;

    /// Helper: create an `Arc<Mutex<CStream>>` from a `TestStream`.
    #[allow(clippy::arc_with_non_send_sync)]
    fn cstream_from(backing: &mut TestStream) -> Arc<Mutex<CStream>> {
        let handle = backing as *mut TestStream as CStreamHandle;
        Arc::new(Mutex::new(unsafe {
            CStream::new(handle, test_callbacks())
        }))
    }

    #[test]
    fn composite_empty() {
        let mut stream = CompositeInputStream::new();
        assert_eq!(stream.size().unwrap(), 0);
        assert_eq!(stream.stream_position().unwrap(), 0);
        let mut buf = [0u8; 10];
        assert_eq!(stream.read(&mut buf).unwrap(), 0);
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

        // Read across all streams
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
        composite.read(&mut buf).unwrap();
        assert_eq!(buf[0], 50);

        // Seek from end
        composite.seek(SeekFrom::End(-1)).unwrap();
        assert_eq!(composite.stream_position().unwrap(), 5);
        composite.read(&mut buf).unwrap();
        assert_eq!(buf[0], 60);

        // Seek back to start
        composite.seek(SeekFrom::Start(0)).unwrap();
        composite.read(&mut buf).unwrap();
        assert_eq!(buf[0], 10);
    }

    #[test]
    fn composite_from_rust_streams() {
        let stream1 = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![1, 2, 3])));
        let stream2 = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![4, 5])));
        let stream3 = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![6, 7, 8, 9])));

        let mut composite = CompositeInputStream::new();
        composite.add_stream(stream1, 3);
        composite.add_stream(stream2, 2);
        composite.add_stream(stream3, 4);

        assert_eq!(composite.size().unwrap(), 9);

        // Read across all streams
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
    fn composite_from_rust_streams_seek() {
        let stream1 = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![10, 20, 30])));
        let stream2 = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![40, 50, 60])));

        let mut composite = CompositeInputStream::new();
        composite.add_stream(stream1, 3);
        composite.add_stream(stream2, 3);

        // Seek into second stream
        composite.seek(SeekFrom::Start(4)).unwrap();
        assert_eq!(composite.stream_position().unwrap(), 4);

        let mut buf = [0u8; 1];
        composite.read(&mut buf).unwrap();
        assert_eq!(buf[0], 50);

        // Seek from end
        composite.seek(SeekFrom::End(-1)).unwrap();
        assert_eq!(composite.stream_position().unwrap(), 5);
        composite.read(&mut buf).unwrap();
        assert_eq!(buf[0], 60);

        // Seek back to start
        composite.seek(SeekFrom::Start(0)).unwrap();
        composite.read(&mut buf).unwrap();
        assert_eq!(buf[0], 10);
    }

    #[test]
    fn composite_short_substream_errors() {
        // Sub-stream has 2 bytes of real data but declares size=5.
        // A second sub-stream with 3 bytes follows it.
        let stream1 = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![0xaa, 0xbb])));
        let stream2 = Arc::new(Mutex::new(MemoryInputStream::from_data(vec![1, 2, 3])));

        let mut composite = CompositeInputStream::new();
        composite.add_stream(stream1, 5); // declared 5, actual 2
        composite.add_stream(stream2, 3);

        assert_eq!(composite.size().unwrap(), 8);

        // First read succeeds and returns the real data from stream1
        let mut buf = [0u8; 16];
        let n = composite.read(&mut buf).unwrap();
        assert_eq!(n, 2);
        assert_eq!(&buf[..2], &[0xaa, 0xbb]);

        // Next read must fail: stream1 is exhausted but 3 declared bytes remain.
        // The composite must not silently return Ok(0) — that would hide stream2.
        let err = composite.read(&mut buf).unwrap_err();
        assert_eq!(err.kind(), ErrorKind::UnexpectedEof);
        let msg = err.to_string();
        assert!(
            msg.contains("sub-stream 0"),
            "error should name the sub-stream: {msg}"
        );
    }
}
