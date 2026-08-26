/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Compression and decompression via the system zlib (linked at build time).
//!
//! Supports three formats:
//! - **Raw**: bare deflate (no headers/trailers) -- used for ZIP entries
//! - **Zlib**: deflate + zlib header/trailer -- used for uSWID payloads
//! - **Gzip**: deflate + gzip header/trailer + CRC32 -- used for .gz files, JCAT, kernel config
//!
use std::io::Read;
use std::pin::Pin;

mod zlib;

use zlib::{Deflate, Inflate, ZStream};

/// Error returned by compression and decompression actions
#[derive(Debug)]
pub enum Error {
    Zlib(String),
    StreamEnd,
    SizeExceeded(usize),
}

impl std::error::Error for Error {}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::Zlib(s) => write!(f, "zlib error: {s}"),
            Error::StreamEnd => write!(f, "Stream ended"),
            Error::SizeExceeded(sz) => {
                write!(f, "Maximum decompressed size ({sz} bytes) exceeded")
            }
        }
    }
}

impl From<Error> for crate::Error {
    fn from(e: Error) -> crate::Error {
        let msg = format!("{e}");
        match e {
            Error::StreamEnd => crate::Error::new(crate::ErrorKind::Read, &msg),
            Error::Zlib(_) | Error::SizeExceeded(_) => {
                crate::Error::new(crate::ErrorKind::InvalidData, &msg)
            }
        }
    }
}

/// Compression format, mirrors `FuCompressorFormat` from the C enum.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CompressorFormat {
    /// Raw deflate (no headers).
    Raw,
    /// Zlib (deflate + zlib header/trailer).
    Zlib,
    /// Gzip (deflate + gzip header/trailer + CRC32).
    Gzip,
}

/// Streaming decompressor from a [`Read`] source.
///
/// Wraps any [`Read`] source and decompresses its contents on the fly.
/// The decompressor implements [`Read`] itself, so it can be used anywhere
/// a reader is expected.
///
/// # Example
///
/// ```no_run
/// use std::io::Read;
/// use fwupd::compressor::{Decompressor, CompressorFormat};
///
/// fn decompress_from_reader(source: Box<dyn Read + Send>) -> std::io::Result<Vec<u8>> {
///     let mut decompressor = Decompressor::new(source, CompressorFormat::Gzip)
///         .map_err(|e| std::io::Error::other(e.to_string()))?;
///     let mut output = Vec::new();
///     decompressor.read_to_end(&mut output)?;
///     Ok(output)
/// }
/// ```
pub struct Decompressor {
    source: Box<dyn Read + Send>,
    zstream: Pin<Box<ZStream<Inflate>>>,
    in_buf: Box<[u8]>,
    in_start: usize,
    in_end: usize,
    finished: bool,
}

impl Decompressor {
    /// Create a new decompressor for the given source and format.
    ///
    /// # Errors
    ///
    /// Returns [`Error::Zlib`] if the zlib inflate stream cannot be initialized.
    pub fn new(source: Box<dyn Read + Send>, format: CompressorFormat) -> Result<Self, Error> {
        let zstream = ZStream::inflator(format)?;
        Ok(Self {
            source,
            zstream,
            in_buf: vec![0u8; 32768].into_boxed_slice(),
            in_start: 0,
            in_end: 0,
            finished: false,
        })
    }
}

impl Read for Decompressor {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        if self.finished || buf.is_empty() {
            return Ok(0);
        }

        loop {
            // Refill input buffer if all previous input has been consumed
            if self.in_start >= self.in_end {
                self.in_start = 0;
                self.in_end = self.source.read(&mut self.in_buf)?;
                if self.in_end == 0 {
                    // Source exhausted but zlib didn't signal stream end
                    return Err(std::io::Error::from(std::io::ErrorKind::UnexpectedEof));
                }
            }

            self.zstream
                .as_mut()
                .set_next_in(&self.in_buf[self.in_start..self.in_end]);
            self.zstream.as_mut().set_next_out(buf);

            let rc = self.zstream.as_mut().inflate(zlib::InflateFlag::NoFlush);
            let consumed = (self.in_end - self.in_start) - self.zstream.avail_in();
            self.in_start += consumed;
            let produced = buf.len() - self.zstream.avail_out();

            match rc {
                Ok(()) => {
                    if produced > 0 {
                        return Ok(produced);
                    }
                    // Produced 0 bytes but stream isn't done, loop to feed more input
                }
                Err(Error::StreamEnd) => {
                    self.finished = true;
                    return Ok(produced);
                }
                Err(e) => {
                    return Err(std::io::Error::other(e.to_string()));
                }
            }
        }
    }
}

/// Streaming compressor from a [`Read`] source.
///
/// Wraps any [`Read`] source and compresses its contents on the fly.
/// The compressor implements [`Read`] itself: each call reads uncompressed
/// data from the source, compresses it, and returns the compressed bytes.
///
/// # Example
///
/// ```no_run
/// use std::io::Read;
/// use fwupd::compressor::{Compressor, CompressorFormat};
///
/// fn compress_from_reader(source: Box<dyn Read + Send>) -> std::io::Result<Vec<u8>> {
///     let mut compressor = Compressor::new(source, CompressorFormat::Gzip)
///         .map_err(|e| std::io::Error::other(e.to_string()))?;
///     let mut output = Vec::new();
///     compressor.read_to_end(&mut output)?;
///     Ok(output)
/// }
/// ```
pub struct Compressor {
    source: Box<dyn Read + Send>,
    zstream: Pin<Box<ZStream<Deflate>>>,
    in_buf: Box<[u8]>,
    in_start: usize,
    in_end: usize,
    /// Source has been fully read; we are flushing remaining compressed data.
    flushing: bool,
    /// Deflate stream has signaled `Z_STREAM_END`; no more output.
    finished: bool,
}

impl Compressor {
    /// Create a new compressor for the given source and format.
    ///
    /// # Errors
    ///
    /// Returns [`Error::Zlib`] if the zlib deflate stream cannot be initialized.
    pub fn new(source: Box<dyn Read + Send>, format: CompressorFormat) -> Result<Self, Error> {
        let zstream = ZStream::deflator(6, format)?;
        Ok(Self {
            source,
            zstream,
            in_buf: vec![0u8; 32768].into_boxed_slice(),
            in_start: 0,
            in_end: 0,
            flushing: false,
            finished: false,
        })
    }
}

impl Read for Compressor {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        if self.finished || buf.is_empty() {
            return Ok(0);
        }

        loop {
            // Refill input buffer if all previous input has been consumed
            // and we haven't reached the end of the source yet.
            if self.in_start >= self.in_end && !self.flushing {
                self.in_start = 0;
                self.in_end = self.source.read(&mut self.in_buf)?;
                if self.in_end == 0 {
                    // Source is exhausted, switch to flushing mode.
                    self.flushing = true;
                }
            }

            // Determine the flush flag: once the source is exhausted we
            // must tell zlib to finish so it writes trailers.
            let flag = if self.flushing {
                zlib::DeflateFlag::Finish
            } else {
                zlib::DeflateFlag::NoFlush
            };

            self.zstream
                .as_mut()
                .set_next_in(&self.in_buf[self.in_start..self.in_end]);
            self.zstream.as_mut().set_next_out(buf);

            let rc = self.zstream.as_mut().deflate(flag);
            let consumed = (self.in_end - self.in_start) - self.zstream.avail_in();
            self.in_start += consumed;
            let produced = buf.len() - self.zstream.avail_out();

            match rc {
                Ok(()) => {
                    if produced > 0 {
                        return Ok(produced);
                    }
                    // Produced 0 bytes but stream isn't done, loop to feed more input
                }
                Err(Error::StreamEnd) => {
                    self.finished = true;
                    return Ok(produced);
                }
                Err(e) => {
                    return Err(std::io::Error::other(e.to_string()));
                }
            }
        }
    }
}

/// Decompress `data` using the specified format and return the decompressed
/// data.
///
/// This is a convenience wrapper around [`Decompressor`] for
/// use cases where the input data is already in memory.
///
/// If `max_output_size` is `Some(n)`, decompression stops with an error once
/// the output exceeds `n` bytes.
///
/// # Errors
///
/// Returns [`Error::Zlib`] if decompression fails, or
/// [`Error::SizeExceeded`] if the output exceeds `max_output_size`.
pub fn decompress(
    format: CompressorFormat,
    data: &[u8],
    max_output_size: Option<usize>,
) -> Result<Vec<u8>, Error> {
    let source: Box<dyn Read + Send> = Box::new(std::io::Cursor::new(data.to_vec()));
    let mut decompressor = Decompressor::new(source, format)?;

    let mut out = Vec::new();
    let mut buf = vec![0u8; 32768].into_boxed_slice();

    loop {
        let n = decompressor
            .read(&mut buf)
            .map_err(|e| Error::Zlib(e.to_string()))?;
        if n == 0 {
            break;
        }
        if let Some(max) = max_output_size {
            if out.len().saturating_add(n) > max {
                return Err(Error::SizeExceeded(max));
            }
        }
        out.extend_from_slice(&buf[..n]);
    }

    Ok(out)
}

/// Compress `data` using the specified format with default compression level (6)
/// and return the compressed data.
///
/// This is a convenience wrapper around [`Compressor`] for
/// use cases where the input data is already in memory.
///
/// # Errors
///
/// Returns [`Error::Zlib`] if compression fails.
pub fn compress(format: CompressorFormat, data: &[u8]) -> Result<Vec<u8>, Error> {
    let source: Box<dyn Read + Send> = Box::new(std::io::Cursor::new(data.to_vec()));
    let mut compressor = Compressor::new(source, format)?;

    let mut out = Vec::new();
    compressor
        .read_to_end(&mut out)
        .map_err(|e| Error::Zlib(e.to_string()))?;

    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Round-trip test for all three formats.
    #[test]
    fn roundtrip_all_formats() {
        let data = b"Hello, world! This is a test of the compression system.";
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = compress(format, data).unwrap();
            let decompressed = decompress(format, &compressed, None).unwrap();
            assert_eq!(
                decompressed, data,
                "round-trip failed for format {format:?}"
            );
        }
    }

    /// Test with empty input.
    #[test]
    fn roundtrip_empty() {
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = compress(format, b"").unwrap();
            let decompressed = decompress(format, &compressed, None).unwrap();
            assert_eq!(decompressed, b"", "empty round-trip failed for {format:?}");
        }
    }

    /// Test with large data (>64KB to exercise multiple deflate blocks).
    #[test]
    fn roundtrip_large() {
        let data: Vec<u8> = (0..=255u8).cycle().take(100_000).collect();
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = compress(format, &data).unwrap();
            let decompressed = decompress(format, &compressed, None).unwrap();
            assert_eq!(decompressed, data, "large round-trip failed for {format:?}");
        }
    }

    /// Error handling: corrupt data.
    #[test]
    fn decompress_corrupt_data() {
        let corrupt = b"\x00\x01\x02\x03\x04\x05";
        // Gzip should fail on corrupt data (bad magic number)
        assert!(decompress(CompressorFormat::Gzip, corrupt, None).is_err());
    }

    /// Round-trip test using the streaming Compressor and Decompressor.
    #[test]
    fn streaming_roundtrip() {
        use std::io::Cursor;
        let data = b"ABCDEFGH";
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let source: Box<dyn Read + Send> = Box::new(Cursor::new(data.to_vec()));
            let mut compressor = Compressor::new(source, format).unwrap();
            let mut compressed = Vec::new();
            compressor.read_to_end(&mut compressed).unwrap();
            assert!(
                !compressed.is_empty(),
                "compressor produced no output for {format:?}"
            );

            let source2: Box<dyn Read + Send> = Box::new(Cursor::new(compressed));
            let mut decompressor = Decompressor::new(source2, format).unwrap();
            let mut decompressed = Vec::new();
            decompressor.read_to_end(&mut decompressed).unwrap();
            assert_eq!(
                decompressed, data,
                "streaming round-trip failed for {format:?}"
            );
        }
    }
}
