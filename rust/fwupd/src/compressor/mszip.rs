/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use std::io::{self, Read};
use std::num::NonZeroUsize;
use std::pin::Pin;

use crate::streams::{Stream, StreamImpl};

use super::zlib::{self, DeflateFlag};
use super::{CompressorFormat, Error};

/// Streaming decompressor for MSZIP data as used in MS Cabinet files.
///
/// MSZIP encodes data as a sequence of raw deflate blocks, each
/// preceded by a two-byte `"CK"` signature.  Each block's decompressed
/// output serves as the preset sliding-window dictionary for the next
/// block, allowing back-references to span block boundaries.
pub struct MszipDecompressor {
    source: Stream<StreamImpl>,
    zstream: Pin<Box<zlib::ZStream<zlib::Inflate>>>,
    /// Chunk buffer for reading from `source`.
    in_buf: Box<[u8]>,
    in_start: usize,
    in_end: usize,
    /// Accumulated decompressed output of the block currently being inflated.
    /// Also serves as the dictionary source once the block finishes.
    out_buf: Vec<u8>,
    /// How many bytes of `out_buf` have already been returned to the caller.
    out_pos: usize,
    /// Full decompressed output of the previous block (i.e. `out_buf`),  used as preset dictionary
    /// for the next block's inflate stream.
    dict_buf: Vec<u8>,
    /// True if the next `read()` expects a new block by reading its "CK" prefix.
    expect_new_block: bool,
    /// True when the source is exhausted and all output has been served.
    finished: bool,
}

impl MszipDecompressor {
    /// Create a new MSZIP decompressor for the given source.
    ///
    /// `source` must contain the concatenated compressed payloads of all MSZIP
    /// blocks, each prefixed with `"CK"`.
    ///
    /// # Errors
    ///
    /// Returns [`Error::Zlib`] if the zlib inflate stream cannot be initialized.
    pub fn new(source: Stream<StreamImpl>) -> Result<Self, Error> {
        let zstream = zlib::ZStream::inflator(CompressorFormat::Raw)?;
        Ok(Self {
            source,
            zstream,
            in_buf: vec![0u8; 4096].into_boxed_slice(),
            in_start: 0,
            in_end: 0,
            out_buf: Vec::new(),
            out_pos: 0,
            dict_buf: Vec::new(),
            expect_new_block: true,
            finished: false,
        })
    }

    /// Read one byte via the internal `in_buf`, refilling from `source` as
    /// needed.  Returns `None` on EOF.
    fn read_buffered_byte(&mut self) -> std::io::Result<Option<u8>> {
        if self.in_start >= self.in_end {
            self.in_start = 0;
            let mut guard = self
                .source
                .lock()
                .map_err(|_| std::io::Error::other("Stream is locked"))?;
            self.in_end = guard.read(&mut self.in_buf)?;
        }
        if self.in_start >= self.in_end {
            return Ok(None);
        }
        let b = self.in_buf[self.in_start];
        self.in_start += 1;
        Ok(Some(b))
    }

    /// Read and validate the `"CK"` prefix, then reset the inflate stream
    /// with the previous block's output as the preset dictionary.
    ///
    /// Returns `Ok(true)` if a new block was started, `Ok(false)` on EOF
    /// (no more blocks).
    fn start_next_block(&mut self) -> std::io::Result<bool> {
        let Some(c) = self.read_buffered_byte()? else {
            return Ok(false); // clean EOF between blocks
        };
        let Some(k) = self.read_buffered_byte()? else {
            return Err(io::Error::from(io::ErrorKind::UnexpectedEof));
        };
        if c != b'C' || k != b'K' {
            return Err(io::Error::other(format!(
                "MSZIP: expected 'CK' prefix, got 0x{c:02x}{k:02x}"
            )));
        }

        // Rotate buffers: the finished block's output becomes the dictionary.
        std::mem::swap(&mut self.out_buf, &mut self.dict_buf);
        self.out_buf.clear();
        self.out_pos = 0;

        self.zstream
            .as_mut()
            .reset()
            .map_err(|e| io::Error::other(e.to_string()))?;
        if !self.dict_buf.is_empty() {
            // zlib window is at most 32 KB; pass only the tail.
            let dict = if self.dict_buf.len() > 32768 {
                &self.dict_buf[self.dict_buf.len() - 32768..]
            } else {
                &self.dict_buf
            };
            self.zstream
                .as_mut()
                .set_dictionary(dict)
                .map_err(|e| io::Error::other(e.to_string()))?;
        }
        self.expect_new_block = false;
        Ok(true)
    }
}

impl Read for MszipDecompressor {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        if self.finished || buf.is_empty() {
            return Ok(0);
        }

        loop {
            if self.finished {
                return Ok(0);
            }

            // 1. Return already-decompressed bytes from the current block.
            if self.out_pos < self.out_buf.len() {
                let n = buf.len().min(self.out_buf.len() - self.out_pos);
                buf[..n].copy_from_slice(&self.out_buf[self.out_pos..self.out_pos + n]);
                self.out_pos += n;

                // We only need the last 32 KB of output for the next block's
                // dictionary, if we exceed twice the blocks size
                // drop the first half. This path should never get hit in
                // for conforming files anyway.
                let keep_from = self.out_pos.saturating_sub(32768);
                if keep_from > 32768 {
                    self.out_buf.drain(..keep_from);
                    self.out_pos -= keep_from;
                }
                return Ok(n);
            }

            // 2. Buffer exhausted — start the next block (reads "CK" prefix) or
            // finish if no more blocks are available.
            if self.expect_new_block && !self.start_next_block()? {
                self.finished = true;
                return Ok(0);
            }

            if self.in_start >= self.in_end {
                self.in_start = 0;
                let mut guard = self
                    .source
                    .lock()
                    .map_err(|_| std::io::Error::other("Stream is locked"))?;
                self.in_end = guard.read(&mut self.in_buf)?;
            }

            // 3. Inflate: feed source bytes to zlib, accumulate into out_buf.
            //
            // Unlike the normal Decompressor we can't just pass the bytes to the
            // caller via zlib's set_next_out, we need to keep the last block
            // around to reuse as dictionary for the previous block.
            let mut out_chunk = [0u8; 4096];
            self.zstream
                .as_mut()
                .set_next_in(&self.in_buf[self.in_start..self.in_end]);
            self.zstream.as_mut().set_next_out(&mut out_chunk);

            let in_available = self.in_end - self.in_start;

            let rc = self.zstream.as_mut().inflate(zlib::InflateFlag::NoFlush);

            let consumed = in_available - self.zstream.avail_in();
            self.in_start += consumed;
            let produced = out_chunk.len() - self.zstream.avail_out();
            self.out_buf.extend_from_slice(&out_chunk[..produced]);

            match rc {
                Ok(()) => {
                    if consumed == 0 && produced == 0 {
                        if self.in_end == 0 {
                            // Source exhausted inside a block — truncated deflate stream.
                            return Err(io::Error::from(io::ErrorKind::UnexpectedEof));
                        }
                        return Err(io::Error::other("MSZIP: inflate stalled"));
                    }
                    // continue with loop, in the normal iteration we'll
                    // now trigger the initial condition where we return
                    // already-decompressed bytes.
                }
                Err(Error::StreamEnd) => {
                    // Current block complete, continue with loop for
                    // next block (if any)
                    self.expect_new_block = true;
                }
                Err(e) => return Err(io::Error::other(e.to_string())),
            }
        }
    }
}

/// A single compressed MSZIP block including its `CK` signature.
///
/// Concatenating [`MszipBlock::as_bytes`] for every block in a
/// [`MszipCompressor`] output produces a stream that [`MszipDecompressor`] can
/// consume directly.
pub struct MszipBlock {
    data: Vec<u8>,
    uncompressed_size: usize,
}

impl MszipBlock {
    /// Returns the full block bytes, including the leading `CK` signature.
    #[must_use]
    pub fn as_bytes(&self) -> &[u8] {
        &self.data
    }

    /// Returns the compressed size of the block, including the `CK` signature.
    #[must_use]
    pub fn size(&self) -> usize {
        self.data.len()
    }

    /// Returns the uncompressed size of the block.
    #[must_use]
    pub fn uncompressed_size(&self) -> usize {
        self.uncompressed_size
    }
}

/// Compresses a `dyn Read + Send` source into a sequence of [`MszipBlock`]s.
///
/// Each block covers at most `block_size` uncompressed bytes.  The decompressed
/// output of each block is used as the preset dictionary for the next block's
/// deflate stream, enabling back-references across block boundaries exactly as
/// [`MszipDecompressor`] expects.
pub struct MszipCompressor {
    source: Stream<StreamImpl>,
    block_size: NonZeroUsize,
}

impl MszipCompressor {
    /// Create a new compressor that reads from `source` and emits blocks of at
    /// most `block_size` uncompressed bytes.
    pub fn new(source: Stream<StreamImpl>, block_size: NonZeroUsize) -> Self {
        Self { source, block_size }
    }

    /// Read the source to exhaustion and return the compressed blocks.
    ///
    /// # Errors
    ///
    /// Returns [`Error::Zlib`] if deflate initialization or compression fails,
    /// or if the source returns an I/O error.
    pub fn compress(&mut self) -> Result<Vec<MszipBlock>, Error> {
        let mut blocks = Vec::new();
        let block_size = self.block_size.get();
        let mut in_buf = vec![0u8; block_size];
        // Plaintext of the previous block, used as the preset dictionary.
        let mut dict: Vec<u8> = Vec::new();

        loop {
            // Fill in_buf with up to block_size bytes from source.
            let mut total_read = 0usize;
            while total_read < block_size {
                let mut guard = self.source.lock().map_err(
                    |_| Error::Zlib("Stream is locked".into()), // eh, good enough?
                )?;
                let n = guard
                    .read(&mut in_buf[total_read..])
                    .map_err(|e| Error::Zlib(e.to_string()))?;
                if n == 0 {
                    break;
                }
                total_read += n;
            }

            if total_read == 0 {
                break;
            }

            let plain = &in_buf[..total_read];

            // Compress `plain` as a single raw-deflate block.
            let mut zstream = zlib::ZStream::deflator(6, CompressorFormat::Raw)?;
            if !dict.is_empty() {
                // zlib sliding window is at most 32 KB.
                let d = if dict.len() > 32768 {
                    &dict[dict.len() - 32768..]
                } else {
                    &dict
                };
                zstream.as_mut().set_dictionary(d)?;
            }

            let mut compressed = Vec::new();
            let mut out_chunk = [0u8; 4096];
            let mut in_start = 0usize;
            loop {
                zstream.as_mut().set_next_in(&plain[in_start..]);
                zstream.as_mut().set_next_out(&mut out_chunk);
                let rc = zstream.as_mut().deflate(DeflateFlag::Finish);
                let consumed = (plain.len() - in_start) - zstream.avail_in();
                in_start += consumed;
                let produced = out_chunk.len() - zstream.avail_out();
                compressed.extend_from_slice(&out_chunk[..produced]);
                match rc {
                    Ok(()) => {}
                    Err(Error::StreamEnd) => break,
                    Err(e) => return Err(e),
                }
            }

            let mut block_data = Vec::with_capacity(2 + compressed.len());
            block_data.push(b'C');
            block_data.push(b'K');
            block_data.extend_from_slice(&compressed);
            blocks.push(MszipBlock {
                data: block_data,
                uncompressed_size: total_read,
            });

            // This block's plaintext becomes the dictionary for the next block.
            dict.clear();
            dict.extend_from_slice(plain);

            if total_read < block_size {
                break; // source exhausted
            }
        }

        Ok(blocks)
    }
}

/// Compress `input` as a single MSZIP block (raw deflate, level 6).
///
/// Returns the `CK`-prefixed compressed data.
///
/// # Errors
///
/// Returns [`Error::Zlib`] if compression fails.
pub fn mszip_compress_block(input: &[u8]) -> Result<Vec<u8>, Error> {
    let compressed = crate::compressor::compress(CompressorFormat::Raw, input)?;
    let mut result = Vec::with_capacity(2 + compressed.len());
    result.push(b'C');
    result.push(b'K');
    result.extend_from_slice(&compressed);
    Ok(result)
}

#[cfg(test)]
mod tests {
    use crate::streams::{IntoStream, MemoryInputStream};

    use super::*;
    use std::io::Read;

    fn compress_blocks(blocks: &[&[u8]]) -> Vec<u8> {
        let mut out = Vec::new();
        for block in blocks {
            out.extend_from_slice(&mszip_compress_block(block).unwrap());
        }
        out
    }

    fn decompress_all(data: Vec<u8>) -> Vec<u8> {
        let stream = MemoryInputStream::from_data(data).into_stream();
        let mut dec = MszipDecompressor::new(stream).unwrap();
        let mut out = Vec::new();
        dec.read_to_end(&mut out).unwrap();
        out
    }

    #[test]
    fn roundtrip_single_block() {
        let input = b"hello, world!";
        let compressed = compress_blocks(&[input]);
        assert_eq!(decompress_all(compressed), input);
    }

    #[test]
    fn roundtrip_empty_block() {
        let compressed = compress_blocks(&[b""]);
        assert_eq!(decompress_all(compressed), b"");
    }

    #[test]
    fn roundtrip_multi_block() {
        let block1 = b"first block of data ";
        let block2 = b"second block references first";
        let compressed = compress_blocks(&[block1, block2]);
        let mut expected = block1.to_vec();
        expected.extend_from_slice(block2);
        assert_eq!(decompress_all(compressed), expected);
    }

    #[test]
    fn roundtrip_large_repetitive_data() {
        // Exercises the sliding-window dictionary across block boundaries.
        let block: Vec<u8> = b"abcdefgh".iter().cycle().take(32768).copied().collect();
        let blocks: Vec<&[u8]> = vec![&block, &block];
        let compressed = compress_blocks(&blocks);
        let mut expected = block.clone();
        expected.extend_from_slice(&block);
        assert_eq!(decompress_all(compressed), expected);
    }

    #[test]
    fn compress_block_has_ck_prefix() {
        let compressed = mszip_compress_block(b"test").unwrap();
        assert_eq!(&compressed[..2], b"CK");
    }

    #[test]
    fn decompress_rejects_bad_prefix() {
        // Replace "CK" with "XX" — decompressor must error.
        let mut compressed = mszip_compress_block(b"test").unwrap();
        compressed[0] = b'X';
        let stream = MemoryInputStream::from_data(compressed).into_stream();
        let mut dec = MszipDecompressor::new(stream).unwrap();
        let mut out = Vec::new();
        assert!(dec.read_to_end(&mut out).is_err());
    }

    #[test]
    fn decompress_rejects_truncated_input() {
        let compressed = mszip_compress_block(b"hello world").unwrap();
        let truncated = compressed[..compressed.len() / 2].to_vec();
        let stream = MemoryInputStream::from_data(truncated).into_stream();
        let mut dec = MszipDecompressor::new(stream).unwrap();
        let mut out = Vec::new();
        assert!(dec.read_to_end(&mut out).is_err());
    }

    fn compress_with_compressor(input: &[u8], block_size: NonZeroUsize) -> Vec<u8> {
        let source = MemoryInputStream::from_data(input.to_vec()).into_stream();
        let mut compressor = MszipCompressor::new(source, block_size);
        let blocks = compressor.compress().unwrap();
        let mut out = Vec::new();
        for block in &blocks {
            out.extend_from_slice(block.as_bytes());
        }
        out
    }

    #[test]
    fn compressor_roundtrip_small() {
        let input = b"hello, MSZIP world!";
        let compressed = compress_with_compressor(input, NonZeroUsize::new(32768).unwrap());
        assert_eq!(decompress_all(compressed), input);
    }

    #[test]
    fn compressor_roundtrip_multi_block() {
        // Use a block size smaller than the input to force multiple blocks.
        let input: Vec<u8> = b"abcdefghij".iter().cycle().take(500).copied().collect();
        let compressed = compress_with_compressor(&input, NonZeroUsize::new(100).unwrap());
        assert_eq!(decompress_all(compressed), input);
    }

    #[test]
    fn compressor_roundtrip_cross_block_dict() {
        // Repetitive data whose compressed form benefits from cross-block
        // dictionary references (second block can reference first block's bytes).
        let block: Vec<u8> = b"repeat_me_".iter().cycle().take(32768).copied().collect();
        let mut input = block.clone();
        input.extend_from_slice(&block);
        let compressed = compress_with_compressor(&input, NonZeroUsize::new(32768).unwrap());
        assert_eq!(decompress_all(compressed), input);
    }

    #[test]
    fn compressor_roundtrip_empty_source() {
        let compressed = compress_with_compressor(b"", NonZeroUsize::new(32768).unwrap());
        // Empty source → no blocks → empty stream → decompressor returns empty
        assert_eq!(compressed, b"");
        assert_eq!(decompress_all(compressed), b"");
    }

    #[test]
    fn compressor_block_has_ck_prefix() {
        let source = MemoryInputStream::from_data(b"test".to_vec()).into_stream();
        let mut compressor = MszipCompressor::new(source, NonZeroUsize::new(32768).unwrap());
        let blocks = compressor.compress().unwrap();
        assert_eq!(blocks.len(), 1);
        assert_eq!(&blocks[0].as_bytes()[..2], b"CK");
    }

    #[test]
    fn oversized_block_is_decompressed_correctly() {
        // Produce a single MSZIP block that decompresses to more than 32 KB.
        // out_buf must stay bounded to ~32 KB during streaming.
        let large_input: Vec<u8> = b"A".iter().cycle().take(65536).copied().collect();
        let compressed = compress_blocks(&[&large_input]);
        assert_eq!(decompress_all(compressed), large_input);
    }
}
