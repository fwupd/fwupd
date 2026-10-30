/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Input stream types for fwupd.
//!
//! This module provides Rust implementations of the fwupd input stream types:
//! - [`MemoryInputStream`]: reads from an in-memory byte buffer
//!   the data is owned by the caller
//! - [`FileInputStream`]: reads from a file on disk
//! - [`PartialInputStream`]: a slice/view over another stream
//! - [`CompositeInputStream`]: concatenation of multiple streams
//! - [`CompressorStream`]: compresses data from another stream on the fly
//! - [`DecompressorStream`]: decompresses data from another stream on the fly
//!
//! All stream types implement [`std::io::Read`] and [`std::io::Seek`] for
//! interoperability with the Rust standard library.

mod composite_input_stream;
mod compressor_stream;
mod file_input_stream;
mod memory_input_stream;
mod partial_input_stream;

#[doc(inline)]
pub use composite_input_stream::*;
#[doc(inline)]
pub use compressor_stream::*;
#[doc(inline)]
pub use file_input_stream::*;
#[doc(inline)]
pub use memory_input_stream::*;
#[doc(inline)]
pub use partial_input_stream::*;

use std::{
    collections::VecDeque,
    io::{self, Read, Seek},
    sync::{Arc, Mutex},
};

use crate::{slice::NonEmptySlice, units::ByteCount};

#[derive(Debug)]
pub enum StreamError {
    /// Invalid arguments (e.g., offset out of range).
    InvalidArgs(String),
    /// Seek failed.
    Seek(String),
    /// Read failed.
    Read(String),
    /// I/O error from the underlying system.
    Io(io::Error),
}

impl std::fmt::Display for StreamError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            StreamError::InvalidArgs(s) => write!(f, "Invalid arguments: {s}"),
            StreamError::Seek(s) => write!(f, "Seek failed: {s}"),
            StreamError::Read(s) => write!(f, "Read failed: {s}"),
            StreamError::Io(e) => write!(f, "I/O error: {e}"),
        }
    }
}

impl std::error::Error for StreamError {}

impl From<io::Error> for StreamError {
    fn from(err: io::Error) -> Self {
        StreamError::Io(err)
    }
}

/// Whether a stream supports seeking.
pub trait IsSeekable {
    /// Returns `true` if this stream supports seeking.
    fn is_seekable(&self) -> bool;
}

/// A stream that supports both reading and seeking.
///
/// This is a supertrait of [`Read`] and [`Seek`] used as a trait object
/// in [`PartialInputStream`] and [`CompositeInputStream`] to support both
/// native Rust streams and C-backed streams.
pub trait ReadSeek: Read + Seek {
    // Trait upcasting requires rust 1.76
    fn as_read(&mut self) -> &mut dyn Read;
}

impl<T: Read + Seek> ReadSeek for T {
    fn as_read(&mut self) -> &mut dyn Read {
        self
    }
}

/// Convenience typedef, combined with the [`IntoStream`] this
/// makes the creation of a stream easier:
///
/// ```
/// use fwupd::streams::{MemoryInputStream, Stream, StreamImpl, IntoStream};
/// let memstream = MemoryInputStream::from_data(vec![1, 2, 3]);
///
/// // equivalent to Arc::new(Mutex::new(memstream))
/// let stream: Stream<StreamImpl> = memstream.into_stream();
/// ```
pub type Stream<T> = Arc<Mutex<T>>;

/// Convenience typedef
///
/// The `ReadSeek` is implemented by each stream, Send is just
/// there to shut clippy up in the FFI, we don't really need it
/// since fwupd is single-threaded.
pub type StreamImpl = dyn ReadSeek + Send;

/// Trait for a blanket implementation of `into_stream()`
/// for any sized type.
pub trait IntoStream: Sized {
    fn into_stream(self) -> Stream<Self> {
        Arc::new(Mutex::new(self))
    }
}

impl<T> IntoStream for T {}

/// Trait for searchable streams.
pub trait Searchable: Read + Seek {
    /// Find the first occurrence of `bytes` within the stream and
    /// return the offset, starting at offset `0`.
    ///
    /// See [`find_from_offset`](Searchable::find_from_offset) for more
    /// details.
    ///
    /// # Errors
    ///
    /// Returns any read or seek errors
    fn find(&mut self, bytes: &NonEmptySlice<u8>) -> std::io::Result<Option<usize>> {
        self.find_from_offset(bytes, 0)
    }

    /// Find the first occurrence of `bytes` within the stream and
    /// return the absolute offset.
    ///
    /// The search starts at `offset`, the returned offset is thus
    /// always equal to or larger than `offset`.
    ///
    /// The stream's position after this function is undefined, a
    /// caller must call `seek` to reset it to the desired position.
    ///
    /// # Errors
    ///
    /// Returns any read or seek errors
    fn find_from_offset(
        &mut self,
        bytes: &NonEmptySlice<u8>,
        offset: usize,
    ) -> std::io::Result<Option<usize>> {
        self.seek(std::io::SeekFrom::Start(offset as u64))?;
        let mut buffer = VecDeque::<u8>::with_capacity(ByteCount::<usize>::from_kb(16).count());
        let mut buffer_offset = offset; // abs offset of buffer[0] in the stream

        let mut readbuf = [0u8; ByteCount::<usize>::from_kb(4).count()];
        loop {
            while buffer.len() < bytes.len() {
                let nbytes = self.read(&mut readbuf)?;
                // EOF and we have fewer than bytes.len() so
                // it cannot be a match
                if nbytes == 0 {
                    return Ok(None);
                }
                buffer.extend(&readbuf[..nbytes]);
            }

            let contiguous = buffer.make_contiguous();
            if let Some(pos) = contiguous
                .windows(bytes.len())
                .position(|window| window == bytes.as_slice())
            {
                return Ok(Some(buffer_offset + pos));
            }

            // Failed to find it and we have a buffer >= bytes.
            // We can drop all but the last bytes.len() - 1.
            let ndrain = buffer.len() - (bytes.len() - 1);
            buffer_offset += ndrain;
            buffer.drain(0..ndrain);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::slice::NonEmptySlice;

    /// Search `haystack` for `needle` starting at `offset`.
    fn find(haystack: &[u8], needle: &[u8], offset: usize) -> std::io::Result<Option<usize>> {
        let mut stream = MemoryInputStream::from_data(haystack.to_vec());
        let needle = NonEmptySlice::new(needle).expect("needle must be non-empty");
        stream.find_from_offset(needle, offset)
    }

    #[test]
    fn searchable_find_simple_match() {
        let data = b"hello world";
        assert_eq!(find(data, b"world", 0).unwrap(), Some(6));
        assert_eq!(find(data, b"hello", 0).unwrap(), Some(0));
        assert_eq!(find(data, b"o", 0).unwrap(), Some(4));
    }

    #[test]
    fn searchable_find_no_match() {
        let data = b"hello world";
        assert_eq!(find(data, b"xyz", 0).unwrap(), None);
        // no match, but the needle is a substring of the data
        assert_eq!(find(data, b"hello worlds", 0).unwrap(), None);
    }

    #[test]
    fn searchable_find_returns_first_occurrence() {
        let data = b"abcabc";
        assert_eq!(find(data, b"abc", 0).unwrap(), Some(0));
        assert_eq!(find(data, b"bca", 0).unwrap(), Some(1));
    }

    #[test]
    fn searchable_find_from_nonzero_offset() {
        let data = b"abcabc";
        // offset skips the first occurrence
        assert_eq!(find(data, b"abc", 1).unwrap(), Some(3));
        assert_eq!(find(data, b"abc", 3).unwrap(), Some(3));
        assert_eq!(find(data, b"abc", 4).unwrap(), None);
    }

    #[test]
    fn searchable_find_offset_at_or_beyond_eof() {
        let data = b"abc";
        // offset == len is a valid (empty) position but cannot match
        assert_eq!(find(data, b"a", 3).unwrap(), None);
        assert!(find(data, b"a", 100).is_err());
    }

    #[test]
    fn searchable_find_match_at_last_byte() {
        let data = b"abcdef";
        assert_eq!(find(data, b"f", 0).unwrap(), Some(5));
        assert_eq!(find(data, b"ef", 0).unwrap(), Some(4));
        assert_eq!(find(data, b"def", 0).unwrap(), Some(3));
    }

    #[test]
    fn searchable_find_single_byte_needle() {
        let data = b"aaabaaa";
        assert_eq!(find(data, b"b", 0).unwrap(), Some(3));
        assert_eq!(find(data, b"a", 4).unwrap(), Some(4));
    }

    #[test]
    fn searchable_find_needle_longer_than_data() {
        let data = b"ab";
        assert_eq!(find(data, b"abc", 0).unwrap(), None);
        assert_eq!(find(data, b"abcd", 0).unwrap(), None);
    }

    #[test]
    fn searchable_find_overlapping_needle() {
        // needle overlapping itself: "aa" in "aaa" matches at 0 and 1
        let data = b"aaa";
        assert_eq!(find(data, b"aa", 0).unwrap(), Some(0));
        assert_eq!(find(data, b"aa", 1).unwrap(), Some(1));
        assert_eq!(find(data, b"aa", 2).unwrap(), None);
    }

    #[test]
    fn searchable_find_across_read_chunk_boundary() {
        // The implementation reads in 4 KiB chunks, ensure we find
        // needles that cross the chunk boundary
        let mut data = vec![0xAAu8; ByteCount::<usize>::from_kb(8).count()];
        // First match crosses the 4 KiB chunk boundary, second one is
        // fully in the second chunk.
        let boundary = ByteCount::<usize>::from_kb(4).count();
        data[boundary - 2..boundary + 2].copy_from_slice(&[0x01, 0x02, 0x03, 0x04]);
        data[boundary + 2..boundary + 6].copy_from_slice(&[0x01, 0x02, 0x03, 0x04]);
        assert_eq!(
            find(&data, &[0x01, 0x02, 0x03, 0x04], 0).unwrap(),
            Some(boundary - 2)
        );
        assert_eq!(
            find(&data, &[0x01, 0x02, 0x03, 0x04], boundary - 1).unwrap(),
            Some(boundary + 2)
        );
        assert_eq!(
            find(&data, &[0x01, 0x02, 0x03, 0x04], boundary + 7).unwrap(),
            None
        );
    }

    #[test]
    fn searchable_find_needle_spanning_chunk_boundary() {
        // Needle starts in one 4 KiB chunk and ends in the next one.
        let mut data = vec![0u8; ByteCount::<usize>::from_kb(4).count() + 8];
        let boundary = ByteCount::<usize>::from_kb(4).count();
        data[boundary - 3..boundary + 3].copy_from_slice(&[1, 2, 3, 4, 5, 6]);
        assert_eq!(
            find(&data, &[1, 2, 3, 4, 5, 6], 0).unwrap(),
            Some(boundary - 3)
        );
    }

    #[test]
    fn searchable_find_stream_position_is_resettable() {
        // The docs say the position after find is undefined but the
        // stream must still be usable after a seek.
        let mut stream = MemoryInputStream::from_data(b"hello world".to_vec());
        let needle = NonEmptySlice::new(b"world").unwrap();
        assert_eq!(stream.find_from_offset(needle, 0).unwrap(), Some(6));
        stream.seek(std::io::SeekFrom::Start(0)).unwrap();
        assert_eq!(stream.find_from_offset(needle, 0).unwrap(), Some(6));
    }
}
