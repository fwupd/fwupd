/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! IO helpers for fwupd

use crate::{slice::NonEmptySlice, units::ByteCount};
use std::{
    collections::VecDeque,
    io::{Read, Seek, SeekFrom},
};

/// Extension trait that adds helpers to any [`Read`] + [`Seek`]
/// implementation.
pub trait ReadSeekExt: Read + Seek {
    /// Reads exactly `count` bytes from the stream starting
    /// at `offset`.
    ///
    /// # Errors
    ///
    /// This function returns an error if the seek fails
    /// or the stream has insufficient bytes.
    fn read_exact_at(&mut self, offset: usize, count: usize) -> Result<Vec<u8>, std::io::Error> {
        self.seek(SeekFrom::Start(offset as u64))?;
        let mut buf = vec![0u8; count];
        self.read_exact(&mut buf)?;
        Ok(buf)
    }
}

impl<T: Read + Seek> ReadSeekExt for T {}

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
    use crate::{slice::NonEmptySlice, streams::MemoryInputStream};
    use std::io::Cursor;

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

    #[test]
    fn read_exact_at_u16_le_at_ok() {
        let data: &[u8] = &[0x34, 0x12];
        let b = Cursor::new(data).read_exact_at(0, 2).unwrap();
        assert_eq!(u16::from_le_bytes([b[0], b[1]]), 0x1234);
    }

    #[test]
    fn read_exact_at_u16_le_at_oob() {
        let data: &[u8] = &[0x34];
        assert!(Cursor::new(data).read_exact_at(0, 2).is_err());
    }

    #[test]
    fn read_exact_at_u32_le_at_ok() {
        let data: &[u8] = &[0x78, 0x56, 0x34, 0x12];
        let b = Cursor::new(data).read_exact_at(0, 4).unwrap();
        assert_eq!(u32::from_le_bytes([b[0], b[1], b[2], b[3]]), 0x12345678);
    }

    #[test]
    fn read_exact_at_u32_le_at_oob() {
        let data: &[u8] = &[0x78, 0x56, 0x34];
        assert!(Cursor::new(data).read_exact_at(0, 4).is_err());
    }

    #[test]
    fn read_exact_at_u32_le_at_offset() {
        let data: &[u8] = &[0xFF, 0x78, 0x56, 0x34, 0x12];
        let b = Cursor::new(data).read_exact_at(1, 4).unwrap();
        assert_eq!(u32::from_le_bytes([b[0], b[1], b[2], b[3]]), 0x12345678);
    }

    #[test]
    fn read_exact_at_ok() {
        let data: &[u8] = &[0x01, 0x02, 0x03, 0x04];
        let result = Cursor::new(data).read_exact_at(1, 2).unwrap();
        assert_eq!(result, vec![0x02, 0x03]);
    }

    #[test]
    fn read_exact_at_oob() {
        let data: &[u8] = &[0x01, 0x02];
        assert!(Cursor::new(data).read_exact_at(0, 5).is_err());
    }

    #[test]
    fn read_exact_at_empty() {
        let data: &[u8] = &[0x01, 0x02];
        let result = Cursor::new(data).read_exact_at(0, 0).unwrap();
        assert_eq!(result, [] as [u8; 0]);
    }
}
