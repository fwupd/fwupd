/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! File-backed input stream.
//!
//! Provides [`FileInputStream`], an input stream that reads from a file on
//! disk. Delegates [`Read`] and [`Seek`]
//! directly to the underlying [`std::fs::File`].

use crate::streams::{IsSeekable, StreamError};

use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom};
#[cfg(unix)]
use std::os::fd::{AsRawFd, RawFd};
use std::path::Path;

/// An input stream backed by a file on disk.
pub struct FileInputStream {
    file: File,
}

impl FileInputStream {
    /// Open a file for reading.
    ///
    /// # Errors
    ///
    /// Returns an [`io::Error`](std::io::Error) if the file cannot be opened.
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self, std::io::Error> {
        let file = File::open(path)?;
        Ok(Self { file })
    }

    /// Create a stream from a pre-created [File]. Use this function
    /// for creating a stream when all you have is a unix fd.
    #[must_use]
    pub fn from_file(file: File) -> Self {
        Self { file }
    }
}

impl FileInputStream {
    /// Return the total size of the stream in bytes.
    ///
    /// # Errors
    ///
    /// Returns [`StreamError::Io`] if the file metadata cannot be read, or
    /// [`StreamError::InvalidArgs`] if the file size exceeds `usize`.
    pub fn size(&self) -> Result<usize, StreamError> {
        usize::try_from(self.file.metadata()?.len())
            .map_err(|_| StreamError::InvalidArgs("file size exceeds usize".into()))
    }

    /// Return the underlying file descriptor
    #[cfg(unix)]
    #[must_use]
    pub fn raw_fd(&self) -> RawFd {
        self.file.as_raw_fd()
    }
}

impl IsSeekable for FileInputStream {
    fn is_seekable(&self) -> bool {
        if self.file.metadata().is_ok_and(|md| md.is_file()) {
            return true;
        }
        self.file
            .try_clone()
            .and_then(|mut f| f.stream_position())
            .is_ok()
    }
}

impl Read for FileInputStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.file.read(buf)
    }
}

impl Seek for FileInputStream {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        self.file.seek(pos)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn file_read() {
        let dir = std::env::temp_dir();
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("fwupd-test-file-read");
        std::fs::write(&path, b"hello world").unwrap();

        let mut stream = FileInputStream::open(path.to_str().unwrap()).unwrap();
        assert_eq!(stream.size().unwrap(), 11);

        let mut buf = [0u8; 5];
        assert_eq!(stream.read(&mut buf).unwrap(), 5);
        assert_eq!(&buf, b"hello");

        stream.seek(SeekFrom::Start(6)).unwrap();
        let mut buf = [0u8; 5];
        assert_eq!(stream.read(&mut buf).unwrap(), 5);
        assert_eq!(&buf, b"world");

        assert_eq!(stream.stream_position().unwrap(), 11);

        std::fs::remove_file(&path).unwrap();
    }

    #[test]
    fn file_open_nonexistent() {
        assert!(FileInputStream::open("/nonexistent/path/file").is_err());
    }
}
