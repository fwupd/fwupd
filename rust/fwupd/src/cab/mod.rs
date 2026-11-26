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

impl From<crate::math::OverflowError<usize>> for CabError {
    fn from(value: crate::math::OverflowError<usize>) -> Self {
        CabError::Format(value.to_string())
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

impl From<std::io::Error> for CabError {
    fn from(e: std::io::Error) -> Self {
        CabError::IO(e)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
