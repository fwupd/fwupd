/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! [Microsoft Cabinet Archive](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85))
//! (CAB, `.cab`) file parser and writer.
//!
//! The CAB binary file format is commonly used by firmware updates.
//! This module exposes the base functionality to extract and create CAB archives.
//!
//! # Extraction
//!
//! Files inside a CAB archive are grouped into "Folders". These folders do not
//! reflect directories, they are data groups for optimal packing of files.
//! The CAB format is old enough that it supports a CAB archive spanning multiple
//! (floppy!) disks, including files spanning multiple folders. Files inside a
//! folder share the same compression format, allowing for more
//! efficient space use.
//!
//! This parser does not support multiple (split) archives, the use-case for this
//! died out decades ago. Folders are not exposed in the extraction API - we provide
//! the [`Archive`] and its list of [`FileEntry`].
//!
//! See the [`Archive`] documentation for examples.
//!
//! # Creating CAB archives
//!
//! This module provides the [`ArchiveBuilder`] to compose CAB files. As with the extraction
//! API the builder is a simple implementation tailored to our use-cases within fwupd. For
//! example the caller has no control over the layout of files inside the archive.
//!
//! # Inspection the wire format
//!
//! The [wire] submodule provides the raw structs for parsing the on-disk format. It is
//! provided as facility for implementing debugging tools.

use std::fmt;
use std::io::{Read, Seek, SeekFrom};

mod archive;
pub use archive::{Archive, FileEntry, ParserFlag, ParserFlags};

mod builder;
mod checksum;
mod msdos;
mod path;
pub mod wire;

pub use builder::{ArchiveBuilder, BuilderError, Building, Built, FileBuilder, HasFiles, NoFiles};
pub use checksum::Checksum;
pub use msdos::{MsDosDate, MsDosFileAttr, MsDosFileAttrs, MsDosTime};
pub use path::CabPath;

/// Errors that can occur during CAB parsing or writing.
#[derive(Debug)]
pub enum CabError {
    /// The data is not a valid MS Cabinet archive or contains
    /// structural errors.
    Format(String),
    /// A feature required to parse the archive is not supported
    /// (e.g. unsupported compression method).
    NotSupported(String),
    /// An error occurred during decompression or compression.
    Decompression(String),
    /// A security limit was exceeded (file count, size, etc.).
    Limit(String),
    /// An IO error occurred during read or write.
    IO(std::io::Error),
}

impl fmt::Display for CabError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CabError::Format(msg) => write!(f, "CAB format error: {msg}"),
            CabError::NotSupported(msg) => write!(f, "CAB not supported: {msg}"),
            CabError::Decompression(msg) => write!(f, "CAB decompression error: {msg}"),
            CabError::Limit(msg) => write!(f, "CAB limit exceeded: {msg}"),
            CabError::IO(e) => write!(f, "IO: {e}"),
        }
    }
}

impl From<crate::compressor::Error> for CabError {
    fn from(e: crate::compressor::Error) -> Self {
        CabError::Decompression(e.to_string())
    }
}

impl From<std::str::Utf8Error> for CabError {
    fn from(e: std::str::Utf8Error) -> Self {
        CabError::Format(e.to_string())
    }
}

impl std::error::Error for CabError {}

impl From<CabError> for crate::Error {
    fn from(e: CabError) -> crate::Error {
        let msg = format!("{e}");
        match e {
            CabError::Format(_) | CabError::NotSupported(_) => {
                crate::Error::new(crate::ErrorKind::InvalidFile, &msg)
            }
            CabError::Decompression(_) | CabError::Limit(_) => {
                crate::Error::new(crate::ErrorKind::InvalidData, &msg)
            }
            CabError::IO(ref io_err) => crate::Error::new(io_err.kind().into(), &msg),
        }
    }
}

/// Extension trait that adds `read_bytes_at()` to any [`Read`] + [`Seek`] implementation.
pub(super) trait ReadBytesAtExt: Read + Seek {
    fn read_bytes_at(&mut self, offset: usize, count: usize) -> Result<Vec<u8>, CabError>;
}

impl<T: Read + Seek> ReadBytesAtExt for T {
    fn read_bytes_at(&mut self, offset: usize, count: usize) -> Result<Vec<u8>, CabError> {
        self.seek(SeekFrom::Start(offset as u64))
            .map_err(|e| CabError::Format(format!("seek to 0x{offset:x} failed: {e}")))?;
        let mut buf = vec![0u8; count];
        self.read_exact(&mut buf).map_err(|e| {
            CabError::Format(format!(
                "short read at offset 0x{offset:x}: need {count} bytes: {e}"
            ))
        })?;
        Ok(buf)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    // -----------------------------------------------------------------------
    // read_bytes_at tests
    // -----------------------------------------------------------------------
    #[test]
    fn read_bytes_at_u16_le_at_ok() {
        let data: &[u8] = &[0x34, 0x12];
        let b = Cursor::new(data).read_bytes_at(0, 2).unwrap();
        assert_eq!(u16::from_le_bytes([b[0], b[1]]), 0x1234);
    }

    #[test]
    fn read_bytes_at_u16_le_at_oob() {
        let data: &[u8] = &[0x34];
        assert!(Cursor::new(data).read_bytes_at(0, 2).is_err());
    }

    #[test]
    fn read_bytes_at_u32_le_at_ok() {
        let data: &[u8] = &[0x78, 0x56, 0x34, 0x12];
        let b = Cursor::new(data).read_bytes_at(0, 4).unwrap();
        assert_eq!(u32::from_le_bytes([b[0], b[1], b[2], b[3]]), 0x12345678);
    }

    #[test]
    fn read_bytes_at_u32_le_at_oob() {
        let data: &[u8] = &[0x78, 0x56, 0x34];
        assert!(Cursor::new(data).read_bytes_at(0, 4).is_err());
    }

    #[test]
    fn read_bytes_at_u32_le_at_offset() {
        let data: &[u8] = &[0xFF, 0x78, 0x56, 0x34, 0x12];
        let b = Cursor::new(data).read_bytes_at(1, 4).unwrap();
        assert_eq!(u32::from_le_bytes([b[0], b[1], b[2], b[3]]), 0x12345678);
    }

    #[test]
    fn read_bytes_at_ok() {
        let data: &[u8] = &[0x01, 0x02, 0x03, 0x04];
        let result = Cursor::new(data).read_bytes_at(1, 2).unwrap();
        assert_eq!(result, vec![0x02, 0x03]);
    }

    #[test]
    fn read_bytes_at_oob() {
        let data: &[u8] = &[0x01, 0x02];
        assert!(Cursor::new(data).read_bytes_at(0, 5).is_err());
    }

    #[test]
    fn read_bytes_at_empty() {
        let data: &[u8] = &[0x01, 0x02];
        let result = Cursor::new(data).read_bytes_at(0, 0).unwrap();
        assert_eq!(result, [] as [u8; 0]);
    }

    // -----------------------------------------------------------------------
    // CabError Display
    // -----------------------------------------------------------------------

    #[test]
    fn error_display_format() {
        let e = CabError::Format("bad magic".into());
        assert!(format!("{e}").contains("bad magic"));
    }

    #[test]
    fn error_display_not_supported() {
        let e = CabError::NotSupported("LZX".into());
        assert!(format!("{e}").contains("LZX"));
    }

    #[test]
    fn error_display_decompression() {
        let e = CabError::Decompression("inflate failed".into());
        assert!(format!("{e}").contains("inflate"));
    }

    #[test]
    fn error_display_limit() {
        let e = CabError::Limit("too many files".into());
        assert!(format!("{e}").contains("too many files"));
    }

    #[test]
    fn error_debug() {
        let e = CabError::Format("test".into());
        let s = format!("{e:?}");
        assert!(s.contains("Format"));
    }
}
