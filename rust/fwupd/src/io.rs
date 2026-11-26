/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! IO helpers for fwupd

use crate::{slice::NonEmptySlice, units::ByteCount};
use std::{
    collections::VecDeque,
    io::{Read, Seek, SeekFrom, Write},
    num::NonZeroUsize,
};
use zerocopy::{FromBytes, Immutable, IntoBytes};

fn read_exact_or_remaining<R: Read + ?Sized>(r: &mut R, count: usize) -> std::io::Result<Vec<u8>> {
    let mut buf = vec![0u8; count];
    let mut offset = 0;
    while offset < count {
        match r.read(&mut buf[offset..]) {
            Ok(0) => break,
            Ok(nbytes) => offset += nbytes,
            Err(e) if e.kind() == std::io::ErrorKind::Interrupted => {}
            Err(e) => return Err(e),
        }
    }
    buf.truncate(offset);
    Ok(buf)
}

/// Extension trait that adds helpers to any [`Read`]
/// implementation.
pub trait ReadExt: Read {
    /// Read exactly `count` bytes unless EOF is encountered.
    ///
    /// # Errors
    ///
    /// If this function encounters an error of the kind
    /// [`std::io::ErrorKind::Interrupted`] then the error is ignored and the operation
    /// will continue.
    ///
    /// If this function encounters an "end of file" before completely filling
    /// the buffer, only the available bytes are returned.
    ///
    /// If any other read error is encountered then this function immediately
    /// returns.
    fn read_exact_or_remaining(&mut self, count: usize) -> std::io::Result<Vec<u8>> {
        read_exact_or_remaining(self, count)
    }

    /// Read up to `count` bytes and return the (optionally) NUL-terminated
    /// UTF8 string contained therein. The returned string has a length of
    /// `count` bytes unless EOF is encountered. If the string contains a NUL-byte,
    /// the returned string is the string up to excluding that NUL-byte.
    ///
    /// A string containing only a NUL-byte returns the empty string.
    ///
    /// <div class="warning">
    /// This function always reads `count` bytes (or until EOF, whichever
    /// happens earlier).
    /// </div>
    ///
    /// ```
    /// use std::io::Cursor;
    /// use fwupd::io::ReadExt;
    ///
    /// let mut stream = Cursor::new(b"FW\0UPD\0".to_vec());
    /// // WARNING: reads up to including the `U` but only returns FW
    /// assert_eq!(stream.read_nul_terminated_string(4).unwrap(), "FW");
    /// assert_eq!(stream.read_nul_terminated_string(4).unwrap(), "PD");
    /// ```
    /// Note how the `U` disappears - four bytes are being read and
    /// the stream offset after the first read is at the `P`.
    ///
    /// # Errors
    ///
    /// Returns [`std::io::ErrorKind::InvalidData`] if the string is not
    /// valid UTF8. If the stream ends before `count` bytes are read,
    /// the short string is returned without an error.
    fn read_nul_terminated_string(&mut self, count: usize) -> std::io::Result<String> {
        let buf = self.read_exact_or_remaining(count)?;
        let nul = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
        String::from_utf8(buf[..nul].to_vec())
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, format!("{e}")))
    }

    /// Read a UTF8 string with a length of up to `count` bytes.
    ///
    /// The returned string has a length of `count` bytes unless
    /// EOF is encountered.
    ///
    /// If reading a null-terminated string, use
    /// [`Self::read_nul_terminated_string`].
    ///
    /// ```
    /// use std::io::Cursor;
    /// use fwupd::io::ReadExt;
    ///
    /// let mut stream = Cursor::new(b"fwupd".to_vec());
    /// assert_eq!(stream.read_string(3).unwrap(), "fwu");
    /// assert_eq!(stream.read_string(2).unwrap(), "pd");
    /// ```
    ///
    /// # Errors
    ///
    /// Returns [`std::io::ErrorKind::InvalidData`] if the string is not
    /// valid UTF8. If the stream ends before `count` bytes are read,
    /// the short string is returned without an error.
    fn read_string(&mut self, count: usize) -> std::io::Result<String> {
        let buf = self.read_exact_or_remaining(count)?;
        String::from_utf8(buf)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, format!("{e}")))
    }

    /// Read a value of type `T` from the stream.
    ///
    /// This is just a convenience wrapper around [`zerocopy::FromBytes::read_from_io`].
    ///
    /// ## Reading primitive types
    ///
    /// ```
    /// use std::io::Cursor;
    /// use zerocopy::byteorder::little_endian::U16;
    /// use fwupd::io::ReadExt;
    ///
    /// let mut stream = Cursor::new(vec![0x12, 0x34, 0x56, 0x78]);
    /// let val: U16 = stream.read_type().unwrap();
    /// assert_eq!(u16::from(val), 0x3412);
    /// ```
    /// ## Reading custom structs
    ///
    /// This also works for custom `#[derive(FromBytes)]` structs, e.g.
    /// multi-field firmware headers, as long as they are `#[repr(C)]`
    /// (or `#[repr(C, packed)]` combined with `Unaligned`):
    ///
    /// ```
    /// use std::io::Cursor;
    /// use zerocopy::{FromBytes, Unaligned};
    /// use zerocopy::byteorder::little_endian::{U16, U32};
    /// use fwupd::io::ReadExt;
    ///
    /// #[repr(C, packed)]
    /// #[derive(Debug, FromBytes, Unaligned)]
    /// struct Header {
    ///     magic: U32,
    ///     version: U16,
    /// }
    ///
    /// let mut stream = Cursor::new(vec![0x46, 0x57, 0x00, 0x00, 0x01, 0x00]);
    /// let header: Header = stream.read_type().unwrap();
    /// assert_eq!(u32::from(header.magic), 0x5746); // "FW"
    /// assert_eq!(u16::from(header.version), 1);
    /// ```
    ///
    /// # Errors
    ///
    /// Returns [`std::io::ErrorKind::UnexpectedEof`] if the stream ends
    /// before `T` is fully read.
    fn read_type<T>(&mut self) -> std::io::Result<T>
    where
        T: FromBytes,
    {
        T::read_from_io(&mut *self)
    }
}

impl<R: Read + ?Sized> ReadExt for R {}

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

    /// Read exactly `count` bytes unless EOF is encountered.
    /// See [`ReadExt::read_exact_or_remaining`] for details, this
    /// function takes an extra `offset` argument.
    ///
    /// # Errors
    ///
    /// If this function encounters an error of the kind
    /// [`std::io::ErrorKind::Interrupted`] then the error is ignored and the operation
    /// will continue.
    ///
    /// If this function encounters an "end of file" before completely filling
    /// the buffer, only the available bytes are returned.
    ///
    /// If any other read error is encountered then this function immediately
    /// returns.
    fn read_exact_or_remaining_at(
        &mut self,
        offset: usize,
        count: usize,
    ) -> std::io::Result<Vec<u8>> {
        self.seek(SeekFrom::Start(offset as u64))?;
        read_exact_or_remaining(self, count)
    }

    /// Read an (optionally) NUL-terminated UTF8 string with a maximum
    /// length of `count` bytes from the stream at `offset`, see
    /// [`ReadExt::read_nul_terminated_string`].
    ///
    /// # Errors
    ///
    /// In addition to the errors of [`ReadExt::read_nul_terminated_string`],
    /// this returns an error if the seek to `offset` fails.
    fn read_nul_terminated_string_at(
        &mut self,
        offset: usize,
        count: usize,
    ) -> std::io::Result<String> {
        self.seek(SeekFrom::Start(offset as u64))?;
        self.read_nul_terminated_string(count)
    }

    /// Read a UTF8 string with a length of up to `count` bytes from the
    /// stream at `offset`, see [`ReadExt::read_string`].
    ///
    /// # Errors
    ///
    /// In addition to the errors of [`ReadExt::read_string`], this
    /// returns an error if the seek to `offset` fails.
    fn read_string_at(&mut self, offset: usize, count: usize) -> std::io::Result<String> {
        self.seek(SeekFrom::Start(offset as u64))?;
        self.read_string(count)
    }

    /// Read a value of type `T` from the stream at `offset`, see
    /// [`ReadExt::read_type`].
    ///
    /// This is just a convenience wrapper around [`zerocopy::FromBytes::read_from_io`].
    ///
    /// ## Reading primitive types
    ///
    /// ```
    /// use std::io::Cursor;
    /// use zerocopy::byteorder::little_endian::U16;
    /// use fwupd::io::ReadSeekExt;
    ///
    /// let mut stream = Cursor::new(vec![0xff, 0xff, 0x12, 0x34]);
    /// let val: U16 = stream.read_type_at(2).unwrap();
    /// assert_eq!(u16::from(val), 0x3412);
    /// ```
    /// ## Reading custom structs
    ///
    /// This also works for custom `#[derive(FromBytes)]` structs, e.g.
    /// multi-field firmware headers, as long as they are `#[repr(C)]`
    /// (or `#[repr(C, packed)]` combined with `Unaligned`):
    ///
    /// ```
    /// use std::io::Cursor;
    /// use zerocopy::{FromBytes, Unaligned};
    /// use zerocopy::byteorder::little_endian::{U16, U32};
    /// use fwupd::io::ReadSeekExt;
    ///
    /// #[repr(C, packed)]
    /// #[derive(Debug, FromBytes, Unaligned)]
    /// struct Header {
    ///     magic: U32,
    ///     version: U16,
    /// }
    ///
    /// let mut stream = Cursor::new(vec![0x00, 0x00, 0x46, 0x57, 0x00, 0x00, 0x01, 0x00]);
    /// let header: Header = stream.read_type_at(2).unwrap();
    /// assert_eq!(u32::from(header.magic), 0x5746); // "FW"
    /// assert_eq!(u16::from(header.version), 1);
    /// ```
    ///
    /// # Errors
    ///
    /// In addition to the errors of [`ReadExt::read_type`], this
    /// returns an error if the seek to `offset` fails.
    fn read_type_at<T>(&mut self, offset: usize) -> std::io::Result<T>
    where
        T: FromBytes,
    {
        self.seek(SeekFrom::Start(offset as u64))?;
        self.read_type()
    }
}

impl<T: Read + Seek + ?Sized> ReadSeekExt for T {}

pub trait WriteExt: Write {
    /// Write the given string as NUL-terminated string.
    ///
    /// The empty string writes exactly one NUL-byte, the return value
    /// is thus always greater than zero.
    ///
    /// ## Example
    ///
    /// ```
    /// use std::io::Cursor;
    /// use fwupd::io::WriteExt;
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let count = stream.write_nul_terminated_string("FW").unwrap();
    /// assert_eq!(count, 3);
    /// assert_eq!(stream.into_inner(), b"FW\0");
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let count = stream.write_nul_terminated_string("").unwrap();
    /// assert_eq!(count, 1);
    /// assert_eq!(stream.into_inner(), b"\0");
    /// ```
    ///
    /// # Errors
    ///
    /// Returns the IO error, if any.
    fn write_nul_terminated_string(&mut self, s: &str) -> std::io::Result<usize> {
        self.write_all(s.as_bytes())?;
        self.write_all(&[0])?;
        Ok(s.len() + 1)
    }

    /// Writes a nul-terminated string, padding out to `count` with NUL bytes
    /// if necessary. If count is less than the string length, the string
    /// is truncated at `count - 1`. The written string is guaranteed to
    /// always have a terminating NUL byte.
    ///
    /// Note: truncation happens on byte level and may split multi-byte
    /// UTF-8 characters.
    ///
    /// ## Example
    ///
    /// ```
    /// use std::io::Cursor;
    /// use std::num::NonZeroUsize;
    /// use fwupd::io::WriteExt;
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let count = stream.write_nul_terminated_padded_string("FW", NonZeroUsize::new(4).unwrap()).unwrap();
    /// assert_eq!(count, 4);
    /// assert_eq!(stream.into_inner(), b"FW\0\0");
    ///
    /// // string truncated to fit the NUL-terminator
    /// let mut stream = Cursor::new(vec![]);
    /// stream.write_nul_terminated_padded_string("FWUPD", NonZeroUsize::new(4).unwrap()).unwrap();
    /// assert_eq!(stream.into_inner(), b"FWU\0");
    /// ```
    ///
    /// # Errors
    ///
    /// Returns the IO error, if any.
    fn write_nul_terminated_padded_string(
        &mut self,
        s: &str,
        count: NonZeroUsize,
    ) -> std::io::Result<usize> {
        let count = count.get();
        let s = &s.as_bytes()[..s.len().min(count - 1)];
        self.write_all(s)?;
        self.write_all(&vec![0; count - s.len()])?;
        Ok(count)
    }

    /// Write the given string without a nul-terminator.
    ///
    /// The empty string writes zero bytes, and the return value is thus
    /// zero.
    ///
    /// ## Example
    ///
    /// ```
    /// use std::io::Cursor;
    /// use fwupd::io::WriteExt;
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let count = stream.write_string("FW").unwrap();
    /// assert_eq!(count, 2);
    /// assert_eq!(stream.into_inner(), b"FW");
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let count = stream.write_string("").unwrap();
    /// assert_eq!(count, 0);
    /// assert!(stream.into_inner().is_empty());
    /// ```
    ///
    /// # Errors
    ///
    /// Returns the IO error, if any.
    fn write_string(&mut self, s: &str) -> std::io::Result<usize> {
        self.write_all(s.as_bytes())?;
        Ok(s.len())
    }

    /// Write the given string, padding with `\0` bytes up to `count` if necessary.
    ///
    /// If the string length exceeds `count`, the string is truncated at
    /// `count` bytes (on byte level, possibly splitting multi-byte UTF-8
    /// characters). If the string length is exactly `count`, no NUL-byte
    /// is written. Use [`WriteExt::write_nul_terminated_padded_string`] for
    /// NUL-terminated strings.
    ///
    /// ## Example
    ///
    /// ```
    /// use std::io::Cursor;
    /// use fwupd::io::WriteExt;
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let count = stream.write_padded_string("FW", 4).unwrap();
    /// assert_eq!(count, 4);
    /// assert_eq!(stream.into_inner(), b"FW\0\0");
    ///
    /// // exactly filling the buffer, no NUL byte appended
    /// let mut stream = Cursor::new(vec![]);
    /// stream.write_padded_string("FW", 2).unwrap();
    /// assert_eq!(stream.into_inner(), b"FW");
    ///
    /// // string truncated to `count` bytes
    /// let mut stream = Cursor::new(vec![]);
    /// stream.write_padded_string("FWUPD", 3).unwrap();
    /// assert_eq!(stream.into_inner(), b"FWU");
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let count = stream.write_padded_string("FW", 0).unwrap();
    /// assert_eq!(count, 0);
    /// assert!(stream.into_inner().is_empty());
    /// ```
    ///
    /// # Errors
    ///
    /// Returns the IO error, if any.
    fn write_padded_string(&mut self, s: &str, count: usize) -> std::io::Result<usize> {
        let s = &s.as_bytes()[..s.len().min(count)];
        self.write_all(s)?;
        self.write_all(&vec![0; count - s.len()])?;
        Ok(count)
    }

    /// Write a value of type `T` to the stream.
    ///
    /// Returns the number of bytes written.
    ///
    /// This is just a convenience wrapper around [`zerocopy::IntoBytes::write_to_io`].
    ///
    /// ## Writing primitive types
    ///
    /// ```
    /// use std::io::Cursor;
    /// use zerocopy::byteorder::little_endian::U16;
    /// use fwupd::io::WriteExt;
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let count = stream.write_type(U16::new(0x3412)).unwrap();
    /// assert_eq!(count, 2);
    /// assert_eq!(stream.into_inner(), vec![0x12, 0x34]);
    /// ```
    /// ## Writing custom structs
    ///
    /// This also works for custom `#[derive(IntoBytes)]` structs, e.g.
    /// multi-field firmware headers, as long as they are `#[repr(C)]`
    /// (or `#[repr(C, packed)]` combined with `Unaligned`):
    ///
    /// ```
    /// use std::io::Cursor;
    /// use zerocopy::{IntoBytes, Immutable, Unaligned};
    /// use zerocopy::byteorder::little_endian::{U16, U32};
    /// use fwupd::io::WriteExt;
    ///
    /// #[repr(C, packed)]
    /// #[derive(Debug, IntoBytes, Immutable, Unaligned)]
    /// struct Header {
    ///     magic: U32,
    ///     version: U16,
    /// }
    ///
    /// let mut stream = Cursor::new(vec![]);
    /// let header = Header { magic: U32::new(0x5746), version: U16::new(1) };
    /// let count = stream.write_type(header).unwrap();
    /// assert_eq!(count, 6);
    /// assert_eq!(stream.into_inner(), vec![0x46, 0x57, 0x00, 0x00, 0x01, 0x00]);
    /// ```
    ///
    /// # Errors
    ///
    /// Returns the Io error, if any.
    fn write_type<T>(&mut self, value: T) -> std::io::Result<usize>
    where
        T: IntoBytes + Immutable + Sized,
    {
        value
            .write_to_io(&mut *self)
            .and(Ok(std::mem::size_of::<T>()))
    }
}

impl<W: Write + ?Sized> WriteExt for W {}

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
                match self.read(&mut readbuf) {
                    // EOF and we have fewer than bytes.len() so
                    // it cannot be a match
                    Ok(0) => return Ok(None),
                    Ok(nbytes) => buffer.extend(&readbuf[..nbytes]),
                    Err(e) if e.kind() == std::io::ErrorKind::Interrupted => {}
                    Err(e) => return Err(e),
                }
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

impl<R: Read + Seek + ?Sized> Searchable for R {}

/// Marker trait for fixed-size structs, `repr(C, packed)` structs
/// that have fixed size. This Is just a convenience trait
/// around `std::mem::size_of::<Self>()`
pub trait FixedSizeStruct {
    /// Returns the size of this struct in bytes
    #[must_use]
    fn size_in_bytes() -> usize
    where
        Self: Sized,
    {
        std::mem::size_of::<Self>()
    }
}

impl<S: FromBytes> FixedSizeStruct for S {}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::{big_endian, little_endian};
    use crate::{slice::NonEmptySlice, streams::MemoryInputStream};
    use std::io::Cursor;

    /// Search `haystack` for `needle` starting at `offset`.
    fn find(haystack: &[u8], needle: &[u8], offset: usize) -> std::io::Result<Option<usize>> {
        let mut stream = MemoryInputStream::from_data(haystack.to_vec());
        let needle = NonEmptySlice::new(needle).expect("needle must be non-empty");
        stream.find_from_offset(needle, offset)
    }

    #[test]
    fn writeext_string() {
        let mut stream = Cursor::new(vec![]);
        assert_eq!(stream.write_string("FW").unwrap(), 2);
        assert_eq!(stream.into_inner(), b"FW");

        let mut stream = Cursor::new(vec![]);
        assert_eq!(stream.write_string("").unwrap(), 0);
        assert_eq!(stream.into_inner(), [] as [u8; 0]);
    }

    #[test]
    fn writeext_nul_terminated_string() {
        let mut stream = Cursor::new(vec![]);
        assert_eq!(stream.write_nul_terminated_string("FW").unwrap(), 3);
        assert_eq!(stream.into_inner(), b"FW\0");

        // the empty string still writes the NUL terminator
        let mut stream = Cursor::new(vec![]);
        assert_eq!(stream.write_nul_terminated_string("").unwrap(), 1);
        assert_eq!(stream.into_inner(), b"\0");
    }

    #[test]
    fn writeext_nul_terminated_padded_string() {
        let nz = NonZeroUsize::new;

        // padded with NUL bytes
        let mut stream = Cursor::new(vec![]);
        assert_eq!(
            stream
                .write_nul_terminated_padded_string("FW", nz(4).unwrap())
                .unwrap(),
            4
        );
        assert_eq!(stream.into_inner(), b"FW\0\0");

        // exactly fits, last byte is the NUL terminator
        let mut stream = Cursor::new(vec![]);
        stream
            .write_nul_terminated_padded_string("FW", nz(3).unwrap())
            .unwrap();
        assert_eq!(stream.into_inner(), b"FW\0");

        // string truncated to count - 1 so the NUL terminator always fits
        let mut stream = Cursor::new(vec![]);
        stream
            .write_nul_terminated_padded_string("FWUPD", nz(4).unwrap())
            .unwrap();
        assert_eq!(stream.into_inner(), b"FWU\0");

        // count of one writes just the NUL terminator
        let mut stream = Cursor::new(vec![]);
        stream
            .write_nul_terminated_padded_string("FWUPD", nz(1).unwrap())
            .unwrap();
        assert_eq!(stream.into_inner(), b"\0");
    }

    #[test]
    fn writeext_padded_string() {
        // padded with NUL bytes
        let mut stream = Cursor::new(vec![]);
        assert_eq!(stream.write_padded_string("FW", 4).unwrap(), 4);
        assert_eq!(stream.into_inner(), b"FW\0\0");

        // exactly filling the buffer, no NUL byte appended
        let mut stream = Cursor::new(vec![]);
        stream.write_padded_string("FW", 2).unwrap();
        assert_eq!(stream.into_inner(), b"FW");

        // string truncated to `count` bytes
        let mut stream = Cursor::new(vec![]);
        stream.write_padded_string("FWUPD", 3).unwrap();
        assert_eq!(stream.into_inner(), b"FWU");

        // zero count writes nothing
        let mut stream = Cursor::new(vec![]);
        assert_eq!(stream.write_padded_string("FW", 0).unwrap(), 0);
        assert_eq!(stream.into_inner(), [] as [u8; 0]);
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

    #[test]
    fn string_read_full_buffer() {
        // No NUL byte: all `count` bytes are returned.
        let mut stream = Cursor::new(b"fwupd".to_vec());
        let s = stream.read_nul_terminated_string(5).unwrap();
        assert_eq!(s, "fwupd");
    }

    #[test]
    fn string_read_nul_terminated() {
        // NUL in the middle: bytes up to but excluding the NUL are returned,
        // trailing bytes after the NUL are consumed but discarded.
        let mut stream = Cursor::new(b"fwupd\0xyz".to_vec());
        let s = stream.read_nul_terminated_string(9).unwrap();
        assert_eq!(s, "fwupd");
    }

    #[test]
    fn string_read_empty() {
        // A buffer of only a NUL byte returns the empty string.
        let mut stream = Cursor::new(b"\0".to_vec());
        let s = stream.read_nul_terminated_string(1).unwrap();
        assert_eq!(s, "");

        // NUL first, garbage after: still the empty string.
        let mut stream = Cursor::new(b"\0xyz".to_vec());
        let s = stream.read_nul_terminated_string(4).unwrap();
        assert_eq!(s, "");
    }

    #[test]
    fn string_read_padding() {
        // NUL-padded fixed-width field, common in firmware headers.
        let mut stream = Cursor::new(b"1.2.3\0\0\0\0\0\0\0\0\0\0\0\0".to_vec());
        let s = stream.read_nul_terminated_string(16).unwrap();
        assert_eq!(s, "1.2.3");
    }

    #[test]
    fn string_read_utf8() {
        let mut stream = Cursor::new("héllo\0".as_bytes().to_vec());
        let s = stream.read_nul_terminated_string(7).unwrap();
        assert_eq!(s, "héllo");
    }

    #[test]
    fn string_read_invalid_utf8() {
        let mut stream = Cursor::new(vec![0xff, 0xfe, 0xfd]);
        let err = stream.read_nul_terminated_string(3).unwrap_err();
        assert_eq!(err.kind(), std::io::ErrorKind::InvalidData);
    }

    #[test]
    fn string_read_truncated_stream() {
        // Requesting more bytes than the stream contains: the short
        // string is returned, no error.
        let mut stream = Cursor::new(b"abc\0".to_vec());
        let s = stream.read_nul_terminated_string(8).unwrap();
        assert_eq!(s, "abc");

        // EOF right at the start: empty string.
        let mut stream = Cursor::new(Vec::new());
        let s = stream.read_nul_terminated_string(4).unwrap();
        assert_eq!(s, "");
    }

    #[test]
    fn string_read_zero_count() {
        // Degenerate case: reading zero bytes yields the empty string
        // without consuming anything.
        let mut stream = Cursor::new(b"abc".to_vec());
        let s = stream.read_nul_terminated_string(0).unwrap();
        assert_eq!(s, "");
        let mut rest = vec![0u8; 1];
        let _ = stream.read(&mut rest).unwrap();
        assert_eq!(rest[0], b'a');
    }

    #[test]
    fn string_read_at_correct_offset() {
        let data = b"\x00\x00fwupd\0extra";
        let mut stream = Cursor::new(data.to_vec());
        let s = stream.read_nul_terminated_string_at(2, 5).unwrap();
        assert_eq!(s, "fwupd");
        let s = stream.read_string_at(2, 5).unwrap();
        assert_eq!(s, "fwupd");
    }

    #[test]
    fn string_read_at_invalid_utf8() {
        let data = b"\x00\xff\xfe\xfd";
        let mut stream = Cursor::new(data.to_vec());
        assert!(stream.read_nul_terminated_string_at(1, 3).is_err());
        assert!(stream.read_string_at(1, 3).is_err());
    }

    #[test]
    fn string_read_at_truncated_stream() {
        // Requesting more bytes than available returns a short string.
        let data = b"\x00hello";
        let mut stream = Cursor::new(data.to_vec());
        let s = stream.read_nul_terminated_string_at(1, 100).unwrap();
        assert_eq!(s, "hello");
    }

    #[test]
    fn u24_read_from_stream() {
        // Little-endian: 0x123456, big-endian: 0x654321
        let mut stream = Cursor::new(vec![0x56, 0x34, 0x12, 0x65, 0x43, 0x21]);
        let le: little_endian::U24 = stream.read_type().unwrap();
        let be: big_endian::U24 = stream.read_type().unwrap();
        assert_eq!(u32::from(le), 0x123456);
        assert_eq!(u32::from(be), 0x654321);
    }

    #[test]
    fn u24_read_unaligned_from_stream() {
        // 24-bit values are often preceded by a byte that breaks alignment;
        // zerocopy's Unaligned impl must allow this.
        let mut stream = Cursor::new(vec![0xAA, 0x78, 0x56, 0x34, 0xBB, 0x78, 0x56, 0x34]);
        let _tag: u8 = stream.read_type().unwrap();
        let le: little_endian::U24 = stream.read_type().unwrap();
        assert_eq!(u32::from(le), 0x345678);

        let _tag: u8 = stream.read_type().unwrap();
        let be: big_endian::U24 = stream.read_type().unwrap();
        assert_eq!(u32::from(be), 0x785634);
    }

    #[test]
    fn u24_read_truncated_stream() {
        let mut stream = Cursor::new(vec![0x56, 0x34]);
        let result: std::io::Result<little_endian::U24> = stream.read_type();
        let err = result.unwrap_err();
        assert_eq!(err.kind(), std::io::ErrorKind::UnexpectedEof);

        let mut stream = Cursor::new(vec![0x56, 0x34]);
        let result: std::io::Result<big_endian::U24> = stream.read_type();
        let err = result.unwrap_err();
        assert_eq!(err.kind(), std::io::ErrorKind::UnexpectedEof);
    }

    #[test]
    fn read_type_at_simple() {
        let mut stream = Cursor::new(vec![0xff, 0xff, 0x34, 0x12]);
        let val: little_endian::U24 = stream.read_type_at(0).unwrap();
        // 0xff, 0xff, 0x34 -> LE = 0x34ffff
        assert_eq!(u32::from(val), 0x34ffff);
    }

    #[test]
    fn read_type_at_offset() {
        let mut stream = Cursor::new(vec![0xff, 0xff, 0x56, 0x34, 0x12]);
        let val: little_endian::U24 = stream.read_type_at(2).unwrap();
        assert_eq!(u32::from(val), 0x123456);
    }

    #[test]
    fn read_type_at_seek_beyond_eof() {
        let mut stream = Cursor::new(vec![0x01, 0x02]);
        // Cursor::seek past EOF succeeds; subsequent read returns UnexpectedEof
        let result: std::io::Result<little_endian::U24> = stream.read_type_at(100);
        assert!(result.is_err());
    }

    #[test]
    fn read_type_at_truncated_after_seek() {
        let mut stream = Cursor::new(vec![0x01, 0x02, 0x03]);
        // Only 1 byte remains after offset 2; reading a U24 (3 bytes) must fail
        let result: std::io::Result<little_endian::U24> = stream.read_type_at(2);
        assert_eq!(
            result.unwrap_err().kind(),
            std::io::ErrorKind::UnexpectedEof
        );
    }
}
