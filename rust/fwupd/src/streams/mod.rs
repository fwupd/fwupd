/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Input stream types for fwupd.
//!
//! This module provides Rust implementations of the fwupd input stream types:
//! - [`MemoryInputStream`]: reads from an in-memory byte buffer
//! - [`BorrowedMemoryInputStream`]: reads from an in-memory byte buffer where
//!   the data is owned by the caller
//! - [`FileInputStream`]: reads from a file on disk
//! - [`PartialInputStream`]: a slice/view over another stream
//! - [`CompositeInputStream`]: concatenation of multiple streams
//! - [`CompressorStream`]: compresses data from another stream on the fly
//! - [`DecompressorStream`]: decompresses data from another stream on the fly
//!
//! All stream types implement [`std::io::Read`] and [`std::io::Seek`] for
//! interoperability with the Rust standard library.

mod borrowed_memory_input_stream;
mod composite_input_stream;
mod compressor_stream;
mod file_input_stream;
mod memory_input_stream;
mod partial_input_stream;

#[doc(inline)]
pub use borrowed_memory_input_stream::*;
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

use std::io::{self, Read, Seek};

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
pub trait ReadSeek: Read + Seek {}

impl<T: Read + Seek> ReadSeek for T {}
