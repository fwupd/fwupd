/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Compressing or decompressing input streams.
//!
//! Provides [`CompressorStream`] and [`DecompressorStream`], streams
//! (de)compressing another input stream on the fly.

use crate::compressor::{Compressor, CompressorFormat, Decompressor};
use crate::streams::{IsSeekable, ReadSeek, StreamError};

use std::io::{self, Read, Seek, SeekFrom};
use std::sync::{Arc, Mutex};

/// Used with [`Error::other`](std::io::Error::other) to signal the stream
/// is not seekable.
///
/// This is equivalent to [`ErrorKind::NotSeekable`](std::io::ErrorKind::NotSeekable),
/// which requires a newer rust version than we support.
#[derive(Debug)]
pub struct NotSeekableError {}

impl std::error::Error for NotSeekableError {}

impl std::fmt::Display for NotSeekableError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "stream is not seekable")
    }
}

/// An adapter to bridge between our base stream
/// and the [`Compressor`]/[`Decompressor`]. The latter
/// requires a `Box<dyn Read>` but we can't permanently
/// lock the base stream.
struct StreamReader {
    inner: Arc<Mutex<dyn ReadSeek + Send>>,
}

impl Read for StreamReader {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let mut stream = self
            .inner
            .lock()
            .map_err(|e| io::Error::other(e.to_string()))?;
        stream.read(buf)
    }
}

/// A compressing stream based on another stream.
///
/// This stream compresses the data from the underlying stream on
/// the fly as data is read.
pub struct CompressorStream {
    // Held to keep the base stream Arc alive for the lifetime of this stream.
    _base: Arc<Mutex<dyn ReadSeek + Send>>,
    inner: Box<dyn Read + Send>,
}

impl CompressorStream {
    /// Create a new compressing input stream from a shared stream.
    ///
    /// # Errors
    ///
    /// Returns [`StreamError::InvalidArgs`] if the compressor cannot be
    /// initialized.
    pub fn new(
        base: Arc<Mutex<dyn ReadSeek + Send>>,
        format: CompressorFormat,
    ) -> Result<Self, StreamError> {
        let reader = StreamReader {
            inner: base.clone(),
        };
        let c = Compressor::new(Box::new(reader), format)
            .map_err(|e| StreamError::InvalidArgs(e.to_string()))?;
        Ok(Self {
            _base: base,
            inner: Box::new(c),
        })
    }
}

impl Read for CompressorStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.inner.read(buf)
    }
}

impl Seek for CompressorStream {
    fn seek(&mut self, _pos: SeekFrom) -> io::Result<u64> {
        Err(std::io::Error::other(NotSeekableError {}))
    }
}

impl IsSeekable for CompressorStream {
    fn is_seekable(&self) -> bool {
        false
    }
}

/// A decompressing stream based on another stream.
///
/// This stream decompresses the data from the underlying stream on
/// the fly as data is read.
pub struct DecompressorStream {
    // Held to keep the base stream Arc alive for the lifetime of this stream.
    _base: Arc<Mutex<dyn ReadSeek + Send>>,
    inner: Box<dyn Read + Send>,
}

impl DecompressorStream {
    /// Create a new decompressing input stream from a shared stream.
    ///
    /// # Errors
    ///
    /// Returns [`StreamError::InvalidArgs`] if the decompressor cannot be
    /// initialized.
    pub fn new(
        base: Arc<Mutex<dyn ReadSeek + Send>>,
        format: CompressorFormat,
    ) -> Result<Self, StreamError> {
        let reader = StreamReader {
            inner: base.clone(),
        };
        let c = Decompressor::new(Box::new(reader), format)
            .map_err(|e| StreamError::InvalidArgs(e.to_string()))?;
        Ok(Self {
            _base: base,
            inner: Box::new(c),
        })
    }
}

impl Read for DecompressorStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.inner.read(buf)
    }
}

impl Seek for DecompressorStream {
    fn seek(&mut self, _pos: SeekFrom) -> io::Result<u64> {
        Err(std::io::Error::other(NotSeekableError {}))
    }
}

impl IsSeekable for DecompressorStream {
    fn is_seekable(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::streams::MemoryInputStream;

    fn base_stream(data: Vec<u8>) -> Arc<Mutex<dyn ReadSeek + Send>> {
        Arc::new(Mutex::new(MemoryInputStream::from_data(data)))
    }

    fn compress(data: &[u8], format: CompressorFormat) -> Vec<u8> {
        let source = base_stream(data.to_vec());
        let mut stream = CompressorStream::new(source, format).unwrap();
        let mut out = Vec::new();
        stream.read_to_end(&mut out).unwrap();
        out
    }

    fn decompress(data: &[u8], format: CompressorFormat) -> Vec<u8> {
        let source = base_stream(data.to_vec());
        let mut stream = DecompressorStream::new(source, format).unwrap();
        let mut out = Vec::new();
        stream.read_to_end(&mut out).unwrap();
        out
    }

    #[test]
    fn compressor_roundtrip_all_formats() {
        let data = b"Hello, world! This is a test of the compressor stream.";
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = compress(data, format);
            assert!(
                !compressed.is_empty(),
                "compressor produced no output for {format:?}"
            );
            let decompressed = decompress(&compressed, format);
            assert_eq!(decompressed, data, "round-trip failed for {format:?}");
        }
    }

    /// Round-trip with empty input.
    #[test]
    fn compressor_roundtrip_empty() {
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = compress(b"", format);
            let decompressed = decompress(&compressed, format);
            assert_eq!(decompressed, b"", "empty round-trip failed for {format:?}");
        }
    }

    #[test]
    fn compressor_roundtrip_large() {
        let data: Vec<u8> = (0..=255u8).cycle().take(100_000).collect();
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = compress(&data, format);
            let decompressed = decompress(&compressed, format);
            assert_eq!(decompressed, data, "large round-trip failed for {format:?}");
        }
    }

    #[test]
    fn compressor_not_seekable() {
        let source = base_stream(vec![1, 2, 3]);
        let mut stream = CompressorStream::new(source, CompressorFormat::Gzip).unwrap();
        assert!(!stream.is_seekable());
        assert!(stream.seek(SeekFrom::Start(0)).is_err());
    }

    #[test]
    fn compressor_actually_compresses() {
        // Highly compressible: 10000 identical bytes
        let data = vec![0x42u8; 10_000];
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = compress(&data, format);
            assert!(
                compressed.len() < data.len(),
                "compressed size ({}) should be < original ({}) for {:?}",
                compressed.len(),
                data.len(),
                format,
            );
        }
    }

    #[test]
    fn compressor_small_reads() {
        let data = b"ABCDEFGH";
        let source = base_stream(data.to_vec());
        let mut stream = CompressorStream::new(source, CompressorFormat::Gzip).unwrap();
        let mut compressed = Vec::new();
        let mut buf = [0u8; 1];
        loop {
            let n = stream.read(&mut buf).unwrap();
            if n == 0 {
                break;
            }
            compressed.push(buf[0]);
        }
        assert!(!compressed.is_empty());
        // Verify the compressed data is valid by decompressing it
        let decompressed = decompress(&compressed, CompressorFormat::Gzip);
        assert_eq!(decompressed, data);
    }

    #[test]
    fn decompressor_not_seekable() {
        // First compress something to get valid compressed data
        let compressed = compress(b"test", CompressorFormat::Gzip);
        let source = base_stream(compressed);
        let mut stream = DecompressorStream::new(source, CompressorFormat::Gzip).unwrap();
        assert!(!stream.is_seekable());
        assert!(stream.seek(SeekFrom::Start(0)).is_err());
    }

    #[test]
    fn decompressor_corrupt_data() {
        let corrupt = vec![0x00, 0x01, 0x02, 0x03, 0x04, 0x05];
        let source = base_stream(corrupt);
        let mut stream = DecompressorStream::new(source, CompressorFormat::Gzip).unwrap();
        let mut out = Vec::new();
        assert!(stream.read_to_end(&mut out).is_err());
    }

    #[test]
    fn decompressor_wrong_format() {
        let compressed = compress(b"test data for format mismatch", CompressorFormat::Gzip);
        let source = base_stream(compressed);
        let mut stream = DecompressorStream::new(source, CompressorFormat::Zlib).unwrap();
        let mut out = Vec::new();
        // Gzip data fed to a Zlib decompressor should fail
        assert!(stream.read_to_end(&mut out).is_err());
    }

    #[test]
    fn decompressor_small_reads() {
        let data = b"ABCDEFGHIJKLMNOP";
        let compressed = compress(data, CompressorFormat::Zlib);
        let source = base_stream(compressed);
        let mut stream = DecompressorStream::new(source, CompressorFormat::Zlib).unwrap();
        let mut decompressed = Vec::new();
        let mut buf = [0u8; 1];
        loop {
            let n = stream.read(&mut buf).unwrap();
            if n == 0 {
                break;
            }
            decompressed.push(buf[0]);
        }
        assert_eq!(decompressed, data);
    }

    #[test]
    fn decompressor_read_after_eof() {
        let data = b"some data";
        let compressed = compress(data, CompressorFormat::Gzip);
        let source = base_stream(compressed);
        let mut stream = DecompressorStream::new(source, CompressorFormat::Gzip).unwrap();
        let mut out = Vec::new();
        stream.read_to_end(&mut out).unwrap();
        assert_eq!(out, data);

        // Further reads should return 0
        let mut buf = [0u8; 10];
        assert_eq!(stream.read(&mut buf).unwrap(), 0);
        assert_eq!(stream.read(&mut buf).unwrap(), 0);
    }

    #[test]
    fn decompressor_zero_length_read() {
        let compressed = compress(b"data", CompressorFormat::Gzip);
        let source = base_stream(compressed);
        let mut stream = DecompressorStream::new(source, CompressorFormat::Gzip).unwrap();
        let mut buf = [0u8; 0];
        assert_eq!(stream.read(&mut buf).unwrap(), 0);
    }

    #[test]
    fn decompress_stream_reads_compressor_output() {
        let data = b"cross-compatibility test data";
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = crate::compressor::compress(format, data).unwrap();
            let decompressed = decompress(&compressed, format);
            assert_eq!(
                decompressed, data,
                "DecompressorStream couldn't read compressor output for {format:?}"
            );
        }
    }

    #[test]
    fn compressor_stream_output_readable_by_decompress() {
        let data = b"cross-compatibility test data";
        for format in [
            CompressorFormat::Raw,
            CompressorFormat::Zlib,
            CompressorFormat::Gzip,
        ] {
            let compressed = compress(data, format);
            let decompressed = crate::compressor::decompress(format, &compressed, None).unwrap();
            assert_eq!(
                decompressed, data,
                "compressor::decompress couldn't read CompressorStream output for {format:?}"
            );
        }
    }
}
