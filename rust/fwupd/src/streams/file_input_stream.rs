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
        // do not trust `metadata().is_file()` as a proxy for seekability: special
        // filesystems such as efivarfs, procfs and sysfs expose regular files that
        // report a size via stat() but fail every lseek() with ESPIPE. Probe the
        // file descriptor directly instead.
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

    #[test]
    fn file_is_seekable() {
        let dir = std::env::temp_dir();
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("fwupd-test-file-seekable");
        std::fs::write(&path, b"hello").unwrap();

        let stream = FileInputStream::open(path.to_str().unwrap()).unwrap();
        assert!(stream.is_seekable());

        std::fs::remove_file(&path).unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn socket_is_not_seekable() {
        use std::os::fd::{FromRawFd, IntoRawFd};
        use std::os::unix::net::UnixStream;

        // a socket -- like a pipe, or an efivarfs entry -- is a valid file
        // descriptor that reads fine but fails every lseek() with ESPIPE, so it
        // must not be reported as seekable
        let (sock, _peer) = UnixStream::pair().unwrap();
        let file = unsafe { File::from_raw_fd(sock.into_raw_fd()) };
        let stream = FileInputStream::from_file(file);
        assert!(!stream.is_seekable());
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn regular_file_but_unseekable_is_not_seekable() {
        use std::fs::OpenOptions;
        use std::os::unix::fs::OpenOptionsExt;

        // an O_PATH descriptor reports as a regular file via fstat() but fails
        // every lseek() -- exactly like an efivarfs entry, where is_file() is
        // true yet seeking is unsupported. This guards against reintroducing an
        // is_file() short-circuit in is_seekable().
        const O_PATH: i32 = 0o10_000_000;
        let dir = std::env::temp_dir();
        let path = dir.join("fwupd-test-opath");
        std::fs::write(&path, b"hello").unwrap();

        let file = OpenOptions::new()
            .read(true)
            .custom_flags(O_PATH)
            .open(&path)
            .unwrap();
        assert!(file.metadata().unwrap().is_file());
        let stream = FileInputStream::from_file(file);
        assert!(!stream.is_seekable());

        std::fs::remove_file(&path).unwrap();
    }
}
