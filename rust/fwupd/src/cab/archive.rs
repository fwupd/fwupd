/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! CAB file parser.
//!
//! This module contains human-readable equivalents of the structs
//! in the [wire] module. These structs provide a `read` function
//! that reads themselves plus any trailing variable length or optional
//! data.
//!
//! For details on the wire format, see the
//! [Microsoft Cabinet Format](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85))
//! documentation.
//!
//!

use super::wire;
use super::wire::{CFCompressionType, CFFolderIndex, CFHeaderFlags, FixedSizeIO};
use super::ReadBytesAtExt;
use super::{CabError, CabPath};
use super::{MsDosDate, MsDosFileAttrs, MsDosTime};
use std::io::Read;
use std::str::Utf8Error;
use std::sync::{Arc, Mutex};

use std::ffi::CString;

use crate::compressor::MszipDecompressor;
use crate::streams::{
    CompositeInputStream, IntoStream, MemoryInputStream, PartialInputStream, ReadSeek,
};
use crate::units::ByteCount;
use crate::{declare_bitflags, Bitflags};

// Some arbitrary limits chosen by us
const MAX_STRING_LENGTH: usize = ByteCount::from_kb(4).count();
const MAX_BLOCK_UNCOMP: usize = ByteCount::from_kb(32).count();
pub(crate) const MAX_FILES: usize = 1024;
const MAX_FOLDERS: usize = MAX_FILES; // our own builder puts each file in a folder
const MAX_RESERVED: usize = ByteCount::from_mb(1).count();
const MAX_FOLDER_UNCOMP: usize = ByteCount::from_mb(256).count();

/// The id assigned to a set of cabinet files. Used
/// if the cabinet file comprises multiple disks and/or files.
///
/// Note: We don't support archives with a set, so this doesn't need
/// to be pub.
#[derive(Clone, Debug, PartialEq, Eq)]

struct CabSet {
    pub id: u16,
    pub prev_cab_file_name: Option<CFString>,
    pub prev_cab_disk_name: Option<CFString>,
    pub next_cab_file_name: Option<CFString>,
    pub next_cab_disk_name: Option<CFString>,
}

/// A parsed and validated [`wire::CFHeader`].
///
/// Reading this struct reads the whole header including any
/// optional and variable-length bytes.
#[derive(Clone, Debug, PartialEq, Eq)]
struct CFHeader {
    pub cab_size_in_bytes: u32,
    pub offset_first_cffile: u32,
    pub version_minor: u8,
    pub version_major: u8,
    pub nfolders: u16,
    pub nfiles: u16,
    pub flags: CFHeaderFlags,
    pub cabinet_set: Option<CabSet>,

    pub per_cab_reserved_bytes: u16,
    pub per_folder_reserved_bytes: u8,
    pub per_data_reserved_bytes: u8,

    size_on_disk: usize,
}

impl CFHeader {
    /// Read the `CFHeader` entry and all trailing optional bytes.
    ///
    /// Returns `Self` and the number of bytes read from the source.
    ///
    /// # Errors
    ///
    /// Returns an error if the read or validation fails.
    #[allow(clippy::too_many_lines)]
    pub fn read(
        src: &mut (impl ReadSeek + ?Sized),
        offset: usize,
        max_reserved_size: usize,
    ) -> Result<(Self, usize), CabError> {
        let mut off = offset;

        let (raw_header, nbytes) = wire::CFHeader::read(src, offset)?;
        off += nbytes;

        if raw_header.signature.get() != wire::CF_SIGNATURE {
            return Err(CabError::Format(format!(
                "bad signature: 0x{:08x}, expected 0x{:08x}",
                raw_header.signature.get(),
                wire::CF_SIGNATURE
            )));
        }

        if raw_header.versionMajor != wire::CF_MAJOR_VERSION
            || raw_header.versionMinor != wire::CF_MINOR_VERSION
        {
            return Err(CabError::Format(format!(
                "unsupported version {}.{}",
                raw_header.versionMajor, raw_header.versionMinor
            )));
        }

        // Process optional reserve header
        let header_flags = CFHeaderFlags::from_bits(raw_header.flags.get());
        let reserve = if header_flags.contains(wire::CFHeaderFlag::HasReserve) {
            let (raw_reserve, nbytes) = wire::CFHeaderReserve::read(src, off)?;
            off += nbytes;

            let sz_header = raw_reserve.cbCFHeader.get() as usize;
            if sz_header > max_reserved_size {
                return Err(CabError::Limit(format!(
                    "reserved header size {sz_header} exceeds maximum {max_reserved_size}"
                )));
            }

            // Skip over per-cabinet reserved data after CFHeader
            // let _ = src.read_bytes_at(offset, sz_header)?;
            off += sz_header;
            Some(raw_reserve)
        } else {
            None
        };

        let header_flags = CFHeaderFlags::from_bits(raw_header.flags.get());
        let (prev_file, prev_disk) = if header_flags.contains(wire::CFHeaderFlag::HasPrev) {
            let (file, nbytes) = CFString::read(src, off, MAX_STRING_LENGTH)?; // previous cabinet name
            off += nbytes;
            let (disk, nbytes) = CFString::read(src, off, MAX_STRING_LENGTH)?; // previous disk name
            off += nbytes;

            let file: Option<CFString> = if file.len_including_nul() > 1 {
                Some(file)
            } else {
                None
            };
            let disk: Option<CFString> = if disk.len_including_nul() > 1 {
                Some(disk)
            } else {
                None
            };
            (file, disk)
        } else {
            (None, None)
        };
        let (next_file, next_disk) = if header_flags.contains(wire::CFHeaderFlag::HasNext) {
            let (file, nbytes) = CFString::read(src, off, MAX_STRING_LENGTH)?; // next cabinet name
            off += nbytes;
            let (disk, nbytes) = CFString::read(src, off, MAX_STRING_LENGTH)?; // next disk name
            off += nbytes;

            let file: Option<CFString> = if file.len_including_nul() > 1 {
                Some(file)
            } else {
                None
            };
            let disk: Option<CFString> = if disk.len_including_nul() > 1 {
                Some(disk)
            } else {
                None
            };
            (file, disk)
        } else {
            (None, None)
        };

        let cab_set = if raw_header.setID != 0 {
            Some(CabSet {
                id: raw_header.setID.get(),
                prev_cab_file_name: prev_file,
                prev_cab_disk_name: prev_disk,
                next_cab_file_name: next_file,
                next_cab_disk_name: next_disk,
            })
        } else {
            None
        };

        let size_on_disk = off - offset;
        Ok((
            Self {
                cab_size_in_bytes: raw_header.cbCabinet.get(),
                offset_first_cffile: raw_header.coffFiles.get(),
                version_major: raw_header.versionMajor,
                version_minor: raw_header.versionMinor,
                nfolders: raw_header.cFolders.get(),
                nfiles: raw_header.cFiles.get(),
                flags: header_flags,

                per_cab_reserved_bytes: reserve.map_or(0, |r| r.cbCFHeader.get()),
                per_folder_reserved_bytes: reserve.map_or(0, |r| r.cbCFFolder),
                per_data_reserved_bytes: reserve.map_or(0, |r| r.cbCFData),
                cabinet_set: cab_set,
                size_on_disk,
            },
            off - offset,
        ))
    }
}

/// The human-compatible version of the [`wire::CFFolder`].
///
/// Reading this struct reads the whole header including any
/// optional and variable-length bytes.
#[derive(Debug, Clone, Copy)]
struct CFFolder {
    offset_first_data_block: usize,
    ndatablocks: usize,
    compression_type: CFCompressionType,
}

impl CFFolder {
    /// Returns `Self` and the number of bytes read
    ///
    /// # Errors
    ///
    /// Returns an error if the read or validation fails
    pub fn read(
        src: &mut (impl ReadSeek + ?Sized),
        offset: usize,
        cffolder_reserved_size: u8,
    ) -> Result<(Self, usize), CabError> {
        let mut off = offset;
        let (raw, nbytes) = wire::CFFolder::read(src, offset)?;
        off += nbytes;

        // let _ = src.read_bytes_at(off, cffolder_reserved_size)
        off += cffolder_reserved_size as usize;

        let size_on_disk = off - offset;
        Ok((
            Self {
                offset_first_data_block: raw.coffCabStart.get() as usize,
                ndatablocks: raw.cCFData.get() as usize,
                compression_type: CFCompressionType::try_from(raw.typeCompress)?,
            },
            size_on_disk,
        ))
    }
}

/// A NUL-terminated string in a CAB file.
///
/// Reading this struct reads the whole header including any
/// optional and variable-length bytes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CFString {
    pub cstr: CString,
    len_including_nul: usize,
}

impl CFString {
    /// Read a NUL-terminated string at the given offset, up to `max` bytes.
    ///
    /// # Errors
    ///
    /// Returns an error if the read or validation fails
    pub fn read(
        mut src: &mut (impl ReadSeek + ?Sized),
        offset: usize,
        max: usize,
    ) -> Result<(Self, usize), CabError> {
        let mut buf = Vec::with_capacity(256.min(max));
        let mut len = 0;
        loop {
            let b = src.read_bytes_at(offset + len, 1)?;
            len += 1;
            if b[0] == 0 {
                let cstr =
                    CString::new(buf).map_err(|_| CabError::Format("Too many NUL-bytes".into()))?;
                return Ok((
                    Self {
                        cstr,
                        len_including_nul: len,
                    },
                    len,
                ));
            }
            if len > max {
                return Err(CabError::Format("NUL-terminated string too long".into()));
            }
            buf.push(b[0]);
        }
    }

    /// Returns the length of the string including the NUL byte.
    #[must_use]
    pub fn len_including_nul(&self) -> usize {
        self.len_including_nul
    }

    /// Return this string as a byte slice, including the terminating NUL byte.
    #[must_use]
    pub fn as_bytes_with_nul(&self) -> &[u8] {
        self.cstr.as_bytes_with_nul()
    }
}

impl TryFrom<&CFString> for String {
    type Error = std::ffi::IntoStringError;

    fn try_from(s: &CFString) -> Result<Self, Self::Error> {
        s.cstr.clone().into_string()
    }
}

impl TryFrom<CFString> for String {
    type Error = std::ffi::IntoStringError;

    fn try_from(s: CFString) -> Result<Self, Self::Error> {
        s.cstr.into_string()
    }
}

impl From<String> for CFString {
    fn from(s: String) -> Self {
        CFString::from(s.as_str())
    }
}

impl From<&str> for CFString {
    fn from(s: &str) -> Self {
        // A CFString is a null-terminated string anyway so
        // quietly truncate.
        let truncated = s.split('\0').next().unwrap_or("");
        Self {
            len_including_nul: truncated.len() + 1,
            cstr: CString::new(truncated).unwrap(),
        }
    }
}

impl std::fmt::Display for CFString {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.cstr.to_string_lossy())
    }
}

/// A [`CFString`] that is guaranteed to be UTF-8 compatible.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(super) struct Utf8CFString(pub(super) CFString);

impl Utf8CFString {
    /// Create a new `Utf8CFString` or fail if the
    /// input string `s` is not utf-8 compatible.
    pub fn new(s: CFString) -> Result<Self, Utf8Error> {
        let _ = s.cstr.to_str()?;
        Ok(Utf8CFString(s))
    }
}

impl std::ops::Deref for Utf8CFString {
    type Target = CFString;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl AsRef<str> for Utf8CFString {
    fn as_ref(&self) -> &str {
        self.0.cstr.to_str().unwrap()
    }
}

impl From<&Utf8CFString> for String {
    fn from(s: &Utf8CFString) -> Self {
        let cstr: &str = s.as_ref();
        String::from(cstr)
    }
}

impl std::fmt::Display for Utf8CFString {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", String::from(self))
    }
}

/// The human-compatible version of a [`wire::CFFile`].
///
/// Reading this struct reads the whole header including any
/// optional and variable-length bytes.
#[derive(Debug, Clone)]
struct CFFile {
    sz_uncompressed: usize,
    offset_in_folder: usize,
    folder_idx: CFFolderIndex,
    date: MsDosDate,
    time: MsDosTime,
    attributes: MsDosFileAttrs,
    name: CFString,
}

impl CFFile {
    /// Returns `Self` and the number of bytes read
    ///
    /// # Errors
    ///
    /// Returns an error if the read or validation fails
    pub fn read(
        src: &mut (impl ReadSeek + ?Sized),
        offset: usize,
    ) -> Result<(Self, usize), CabError> {
        let mut off = offset;
        let (raw, nbytes) = wire::CFFile::read(src, offset)?;
        off += nbytes;

        let (name, nbytes) = CFString::read(src, off, 1024)?;
        off += nbytes;

        let _ = String::try_from(&name)
            .map_err(|_| CabError::Format("invalid UTF-8 in filename".into()))?;

        let size_on_disk = off - offset;
        Ok((
            Self {
                sz_uncompressed: raw.cbFile.get() as usize,
                offset_in_folder: raw.uoffFolderStart.get() as usize,
                folder_idx: raw.iFolder.into(),
                date: raw.date,
                time: raw.time,
                attributes: MsDosFileAttrs::from_bits(raw.attribs.get()),
                name,
            },
            size_on_disk,
        ))
    }
}

/// The human-compatible version of the [`wire::CFData`].
///
/// Reading this struct reads the whole header including any
/// optional and variable-length bytes.
#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
struct CFData {
    pub checksum: u32,                     // checksum of this data block
    pub compressed_size_in_bytes: usize,   // compressed size
    pub uncompressed_size_in_bytes: usize, // uncompressed size

    size_on_disk: usize,
}

impl CFData {
    /// Reads this [`CFData`] entry from the source, including the
    /// extra reserved size.
    ///
    /// The payload of the `CFData` is discarded after checksum validation.
    ///
    /// # Errors
    ///
    /// Returns an error if the read or validation of the checksum failed.
    pub fn read(
        mut src: &mut (impl ReadSeek + ?Sized),
        offset: usize,
        cfdata_reserved_size: u8,
        verify_checksum: bool,
    ) -> Result<(Self, usize), CabError> {
        let mut off = offset;
        let (raw, nbytes) = wire::CFData::read(src, offset)?;
        off += nbytes;

        // let _ = src.read_bytes_at(off, cfdata_reserved_size)
        off += cfdata_reserved_size as usize;

        let nbytes_payload = raw.cbData.get() as usize;

        if verify_checksum {
            let payload = src.read_bytes_at(off, nbytes_payload)?;
            CFData::checksum(raw, &payload).map_err(|cs| {
                CabError::Format(format!(
                    "Invalid checksum 0x{cs:08x}, stored checksum is 0x{:08x}",
                    raw.csum.get()
                ))
            })?;
        }
        off += nbytes_payload;

        let size_on_disk = off - offset;
        Ok((
            Self {
                checksum: raw.csum.get(),
                compressed_size_in_bytes: usize::from(raw.cbData.get()),
                uncompressed_size_in_bytes: usize::from(raw.cbUncomp.get()),
                size_on_disk,
            },
            size_on_disk,
        ))
    }

    /// Verify the checksum of this data block including the given
    /// payload.
    ///
    ///
    /// # Errors
    ///
    /// On error returns the calculated (i.e. expected) checksum.
    fn checksum(raw: wire::CFData, payload: &[u8]) -> Result<(), u32> {
        if raw.csum.get() == 0 {
            return Ok(());
        }
        let mut csum = super::Checksum::new_from_cfdata(&raw);
        csum.update(payload);
        if csum.checksum() == raw.csum.get() {
            Ok(())
        } else {
            Err(csum.checksum())
        }
    }
}

/// A single file inside a CAB [Archive].
///
/// To extract the content of the file, use [`std::io::Read`].
/// ```no_run
/// use fwupd::{Bitflags, streams::{ReadSeek, IntoStream}};
/// use fwupd::cab::{Archive, ParserFlags};
/// use std::io::{Cursor, Read};
/// use std::sync::{Arc, Mutex};
///
/// let data: Vec<u8> = std::fs::read("firmware.cab").unwrap();
/// let cursor = Cursor::new(data);
/// let src = cursor.into_stream();
/// let mut archive = Archive::new(src, 0, ParserFlags::empty()).unwrap();
/// for file in archive.files_mut() {
///     let mut bytes: Vec<u8> = Vec::new();
///     file.read_to_end(&mut bytes).unwrap();
///     // ...
/// }
/// ```
pub struct FileEntry {
    header: CFFile,
    path: CabPath,

    stream: PartialInputStream,
}

impl FileEntry {
    /// Returns the file name of this file. This is
    /// the path of the file, or the basename if
    /// the [Archive] was constructed with [`ParserFlag::OnlyBasename`].
    #[must_use]
    pub fn name(&self) -> &CabPath {
        &self.path
    }

    /// The creation date of this file.
    ///
    /// Returns `None` if the date was zero in the file.
    #[must_use]
    pub fn date(&self) -> Option<MsDosDate> {
        if self.header.date == MsDosDate::from(0) {
            None
        } else {
            Some(self.header.date)
        }
    }

    /// The creation time of this file. If this
    /// time is zero it is indistinguishable from 00:00:00.
    #[must_use]
    pub fn time(&self) -> MsDosTime {
        self.header.time
    }

    /// Attributes that apply to this file.
    #[must_use]
    pub fn attributes(&self) -> MsDosFileAttrs {
        self.header.attributes
    }

    /// The size of this file in bytes.
    #[must_use]
    pub fn size(&self) -> usize {
        self.header.sz_uncompressed
    }
}

impl Read for FileEntry {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.stream.read(buf)
    }
}

impl std::fmt::Debug for FileEntry {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FileEntry")
            .field("path", &self.path)
            .finish_non_exhaustive()
    }
}
struct FolderData {
    stream: Arc<Mutex<CompositeInputStream>>,
    uncompressed_size: usize,
}

declare_bitflags! {
    /// Cab archive parsing flags.
    ///
    /// Controls the behavior of the parser when reading an [`Archive`].
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct ParserFlags / ParserFlag : u32 {
        /// Ignore the checksum in the CAB data blocks
        IgnoreChecksum = 0x01;
        /// Reduce any CAB file path to its basename
        ///
        /// Where a CAB file contains multiple identical basenames, this
        /// will trigger the duplicate file name detection.
        OnlyBasename = 0x02;
    }
}

/// Represents a single CAB archive, comprising of multiple files.
///
/// Files inside a CAB archive are grouped into "Folders". These folders do not
/// reflect file system directories, they are merely groups for optimal packing
/// of files. The CAB format is old enough that it supports a CAB archive
/// spanning multiple (floppy!) disks, including files spanning multiple
/// folders. Files inside a folder share the same compression format, allowing
/// for more efficient use of disk space.
///
/// This parser does not support CAB archives split across multiple disks or files.
/// The use-case for this died out decades ago. Folders are not exposed in the
/// extraction API - we only provide the [`Archive`] and its list of [`FileEntry`].
///
/// ## Example
///
/// ```no_run
/// use fwupd::{Bitflags, streams::{ReadSeek, IntoStream}};
/// use fwupd::cab::{Archive, ParserFlags};
/// use std::io::{Cursor, Read};
/// use std::sync::{Arc, Mutex};
///
/// let data: Vec<u8> = std::fs::read("firmware.cab").unwrap();
/// let cursor = Cursor::new(data);
/// let src = cursor.into_stream();
/// let mut archive = Archive::new(src, 0, ParserFlags::empty()).unwrap();
/// for file in archive.files_mut() {
///     let mut bytes = Vec::new();
///     file.read_to_end(&mut bytes).unwrap();
///     // ...
/// }
/// ```
///
/// # Limits
///
/// Several hard-coded limits are applied to an [`Archive`]'s contents,
/// mostly related to allowed length for entities. These lengths are
/// generous enough so that any non-malicious CAB file is considered valid.
///
/// Where a limit is exceeded, a [`CabError::Limit`] is returned by
/// the respective API.
pub struct Archive {
    files: Vec<FileEntry>,
    size_on_disk: usize,
    was_compressed: bool,
}

impl std::fmt::Debug for Archive {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Archive")
            .field("files", &self.files)
            .field("size_on_disk", &self.size_on_disk)
            .finish_non_exhaustive()
    }
}

impl Archive {
    /// Size of this file on disk, in bytes.
    #[must_use]
    pub fn size(&self) -> usize {
        self.size_on_disk
    }

    /// Returns `true` if any folder in this archive used MSZIP compression.
    #[must_use]
    pub fn was_compressed(&self) -> bool {
        self.was_compressed
    }

    /// Return the list of files in this archive
    #[must_use]
    pub fn files(&self) -> &[FileEntry] {
        &self.files
    }

    /// Return the list of files in this archive as mutable slice
    pub fn files_mut(&mut self) -> &mut [FileEntry] {
        &mut self.files
    }

    fn validate_header(header: &CFHeader) -> Result<(), CabError> {
        if header.cabinet_set.as_ref().is_some_and(|set| {
            set.prev_cab_file_name.is_some()
                || set.prev_cab_disk_name.is_some()
                || set.next_cab_file_name.is_some()
                || set.next_cab_disk_name.is_some()
        }) {
            return Err(CabError::NotSupported(
                "chained archive not supported".into(),
            ));
        }

        let nfolders = header.nfolders as usize;
        if nfolders == 0 {
            return Err(CabError::Format("no folders in cabinet".into()));
        }
        if nfolders > MAX_FOLDERS {
            return Err(CabError::Limit(format!(
                "too many folders: {nfolders} (max {MAX_FOLDERS})"
            )));
        }

        let nfiles = header.nfiles as usize;
        if nfiles == 0 {
            return Err(CabError::Format("no files in cabinet".into()));
        }
        if nfiles > MAX_FILES {
            return Err(CabError::Limit(format!(
                "too many files: {nfiles} (max {MAX_FILES})"
            )));
        }

        Ok(())
    }

    /// From the given stream at the given offset parse the CAB archive and return it.
    ///
    /// # Errors
    ///
    /// Returns any CAB file parsing errors.
    #[allow(clippy::too_many_lines, clippy::needless_pass_by_value)]
    pub fn new(
        src: Arc<Mutex<dyn ReadSeek + Send>>,
        offset: usize,
        flags: ParserFlags,
    ) -> Result<Archive, CabError> {
        let mut off = offset;
        let (header, nbytes) = {
            let mut guard = src
                .lock()
                .map_err(|_| CabError::IO(std::io::Error::other("Stream is locked".to_string())))?;
            CFHeader::read(&mut *guard, offset, MAX_RESERVED)?
        };
        off += nbytes; // offset points to first byte after full header

        Self::validate_header(&header)?;

        let nfolders = header.nfolders as usize;
        let nfiles = header.nfiles as usize;
        let mut max_offset = offset;

        // Process folders
        let reserved_folder_bytes = header.per_folder_reserved_bytes;
        let reserved_data_bytes = header.per_data_reserved_bytes;
        let mut folders = Vec::with_capacity(nfolders);
        let mut folder_off = off;
        let mut was_compressed = false;
        for _ in 0..nfolders {
            let mut folder_stream = CompositeInputStream::new();
            let (folder_header, nbytes) = {
                let mut guard = src.lock().map_err(|_| {
                    CabError::IO(std::io::Error::other("Stream is locked".to_string()))
                })?;
                CFFolder::read(&mut *guard, folder_off, reserved_folder_bytes)?
            };
            folder_off += nbytes;

            let mut data_off = offset + folder_header.offset_first_data_block;
            let mut uncompressed_folder_size = 0;
            for _ in 0..folder_header.ndatablocks {
                let (data_header, nbytes) = {
                    let mut guard = src.lock().map_err(|_| {
                        CabError::IO(std::io::Error::other("Stream is locked".to_string()))
                    })?;
                    CFData::read(
                        &mut *guard,
                        data_off,
                        reserved_data_bytes,
                        !flags.contains(ParserFlag::IgnoreChecksum),
                    )?
                };

                let sz_compressed = data_header.compressed_size_in_bytes;
                let sz_uncompressed = data_header.uncompressed_size_in_bytes;
                if sz_uncompressed > MAX_BLOCK_UNCOMP {
                    return Err(CabError::Limit(format!(
                        "uncompressed block size {sz_uncompressed} exceeds {MAX_BLOCK_UNCOMP}"
                    )));
                }
                uncompressed_folder_size += sz_uncompressed;
                if uncompressed_folder_size > MAX_FOLDER_UNCOMP {
                    return Err(CabError::Limit(format!(
                        "folder decompressed size {uncompressed_folder_size} exceeds {MAX_FOLDER_UNCOMP}"
                    )));
                }

                let header_size = nbytes - sz_compressed;
                let datastream = PartialInputStream::from_stream(
                    src.clone(),
                    data_off + header_size,
                    sz_compressed,
                );
                folder_stream.add_stream(datastream.into_stream(), sz_compressed);
                data_off += nbytes; // Eventually points to first byte after data
                max_offset = std::cmp::max(data_off, max_offset);
            }

            let folder_stream = match folder_header.compression_type {
                // Uncompressed folders are easy: we create a new partial
                // stream for each actual byte span and add it to the
                // folder stream, files can later read from that.
                CFCompressionType::None => folder_stream,
                // MsZip is weirder: because of how the decompression works (each
                // block feeds into the next) and that files can reside anywhere
                // in the decompressed data, we need to decompresss all of it.
                //
                // This ironically means that compressed folders take more
                // memory than uncompressed ones because we have to keep it
                // around.
                CFCompressionType::MsZip => {
                    was_compressed = true;
                    let decomp = MszipDecompressor::new(folder_stream.into_stream())?;
                    let mut buf = Vec::with_capacity(uncompressed_folder_size);
                    decomp
                        .take(uncompressed_folder_size as u64 + 1)
                        .read_to_end(&mut buf)
                        .map_err(|e| CabError::Decompression(e.to_string()))?;
                    if buf.len() != uncompressed_folder_size {
                        return Err(CabError::Decompression(format!(
                            "folder decompressed size {} does not match declared size {uncompressed_folder_size}",
                            buf.len()
                        )));
                    }

                    let nbytes = buf.len();
                    let memstream = MemoryInputStream::from_data(buf);

                    let mut composite = CompositeInputStream::new();
                    composite.add_stream(memstream.into_stream(), nbytes);
                    composite
                }
            };

            folders.push(FolderData {
                stream: folder_stream.into_stream(),
                uncompressed_size: uncompressed_folder_size,
            });
        }

        off = offset + header.offset_first_cffile as usize;

        let mut seen_file_names = std::collections::HashSet::new();
        let mut file_headers = Vec::with_capacity(nfiles);
        for _ in 0..nfiles {
            let (file_header, nbytes) = {
                let mut guard = src.lock().map_err(|_| {
                    CabError::IO(std::io::Error::other("Stream is locked".to_string()))
                })?;
                CFFile::read(&mut *guard, off)?
            };
            off += nbytes; // off points to first byte after this file
            file_headers.push(file_header);
        }

        // CAB requires that files are sorted by folder and offset but
        // just in case...
        file_headers.sort_by_key(|h| {
            let folder_idx = u64::from(u16::from(h.folder_idx));
            let offset: u64 = h.offset_in_folder as u64;
            folder_idx << 32 | offset
        });

        let mut files = Vec::with_capacity(nfiles);

        for file_header in file_headers {
            let name = &file_header.name;
            let CFFolderIndex::Index(folder_idx) = file_header.folder_idx else {
                return Err(CabError::NotSupported(format!(
                    "{name} continues across folders."
                )));
            };

            let folder = folders
                .get(folder_idx as usize)
                .ok_or(CabError::Format(format!(
                    "file '{}' references folder {} out of {nfolders}",
                    file_header.name, folder_idx,
                )))?;

            let start = file_header.offset_in_folder;
            let file_size = file_header.sz_uncompressed;
            if start.checked_add(file_size).is_none()
                || start + file_size > folder.uncompressed_size
            {
                return Err(CabError::Format(format!(
                    "file '{}' extends past end of folder data (start=0x{start:x}, size=0x{file_size:x}, folder_size=0x{:x})",
                    file_header.name,
                    folder.uncompressed_size
                )));
            }

            let full_path = CabPath::new(name)?;
            let path = if flags.contains(ParserFlag::OnlyBasename) {
                CabPath::new(&CFString::from(
                    full_path.basename().to_string_lossy().as_ref(),
                ))?
            } else {
                full_path
            };
            let name = path.path();
            if name.is_empty() {
                return Err(CabError::Format("empty filename".into()));
            }

            if !seen_file_names.insert(name.to_string_lossy().into_owned()) {
                return Err(CabError::Format(format!("duplicate filename: {path}")));
            }

            let stream = PartialInputStream::from_stream(
                folder.stream.clone(),
                file_header.offset_in_folder,
                file_header.sz_uncompressed,
            );

            let fdata = FileEntry {
                header: file_header,
                path,
                stream,
            };
            files.push(fdata);
        }

        Ok(Self {
            files,
            size_on_disk: max_offset - offset,
            was_compressed,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::cab::wire::{
        CFCompressionType, CFData, CFFile, CFFolder, CFHeader, FixedSizeIO, CF_SIGNATURE,
    };
    use crate::cab::{Checksum, MsDosFileAttr};
    use crate::streams::IntoStream;
    use crate::Bitflags;
    use std::io::{Cursor, Read};
    use zerocopy::byteorder::little_endian::{U16 as U16LE, U32 as U32LE};

    const CFHEADER_SIZE: u32 = 36;
    const CFFOLDER_SIZE: u32 = 8;
    const CFFILE_SIZE: u32 = 16;
    const CFDATA_SIZE: u32 = 8;

    fn make_archive(data: &[u8], flags: ParserFlags) -> Archive {
        let src = Cursor::new(data.to_vec()).into_stream();
        Archive::new(src, 0, flags).unwrap()
    }

    fn read_file(archive: &mut Archive, idx: usize) -> Vec<u8> {
        let mut bytes = Vec::new();
        archive.files_mut()[idx].read_to_end(&mut bytes).unwrap();
        bytes
    }

    #[test]
    fn parse_bad_signature() {
        let data = [0u8; CFHEADER_SIZE as usize];
        let src = Cursor::new(data.to_vec()).into_stream();
        let err = Archive::new(src, 0, ParserFlags::empty()).unwrap_err();
        assert!(matches!(err, CabError::Format(_)));
    }

    #[test]
    fn parse_bad_version() {
        let mut data = [0u8; CFHEADER_SIZE as usize];
        data[0..4].copy_from_slice(&CF_SIGNATURE.to_le_bytes());
        data[24] = 0; // versionMinor
        data[25] = 2; // versionMajor (unsupported)
        data[26..28].copy_from_slice(&1u16.to_le_bytes()); // cFolders
        data[28..30].copy_from_slice(&1u16.to_le_bytes()); // cFiles
        let src = Cursor::new(data.to_vec()).into_stream();
        let err = Archive::new(src, 0, ParserFlags::empty()).unwrap_err();
        assert!(matches!(err, CabError::Format(_)));
    }

    #[test]
    fn parse_zero_folders() {
        let mut data = [0u8; CFHEADER_SIZE as usize];
        data[0..4].copy_from_slice(&CF_SIGNATURE.to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&0u16.to_le_bytes()); // cFolders = 0
        data[28..30].copy_from_slice(&1u16.to_le_bytes());
        let src = Cursor::new(data.to_vec()).into_stream();
        let err = Archive::new(src, 0, ParserFlags::empty()).unwrap_err();
        assert!(matches!(err, CabError::Format(_)));
    }

    #[test]
    fn parse_zero_files() {
        let mut data = [0u8; CFHEADER_SIZE as usize];
        data[0..4].copy_from_slice(&CF_SIGNATURE.to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&1u16.to_le_bytes());
        data[28..30].copy_from_slice(&0u16.to_le_bytes()); // cFiles = 0
        let src = Cursor::new(data.to_vec()).into_stream();
        let err = Archive::new(src, 0, ParserFlags::empty()).unwrap_err();
        assert!(matches!(err, CabError::Format(_)));
    }

    #[test]
    fn parse_too_many_folders() {
        let mut data = [0u8; CFHEADER_SIZE as usize];
        data[0..4].copy_from_slice(&CF_SIGNATURE.to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&1025u16.to_le_bytes()); // MAX_FOLDERS + 1
        data[28..30].copy_from_slice(&1u16.to_le_bytes());
        let src = Cursor::new(data.to_vec()).into_stream();
        let err = Archive::new(src, 0, ParserFlags::empty()).unwrap_err();
        assert!(matches!(err, CabError::Limit(_)));
    }

    #[test]
    fn parse_too_many_files() {
        let mut data = [0u8; CFHEADER_SIZE as usize];
        data[0..4].copy_from_slice(&CF_SIGNATURE.to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&1u16.to_le_bytes());
        data[28..30].copy_from_slice(&1025u16.to_le_bytes()); // MAX_FILES + 1
        let src = Cursor::new(data.to_vec()).into_stream();
        let err = Archive::new(src, 0, ParserFlags::empty()).unwrap_err();
        assert!(matches!(err, CabError::Limit(_)));
    }

    // Build a minimal valid cab and parse it
    fn build_uncompressed_cab(filename: &str, data: &[u8]) -> Vec<u8> {
        let file_hdr_sz = CFFILE_SIZE + u32::try_from(filename.len()).unwrap() + 1;
        let offset_cffile = CFHEADER_SIZE + CFFOLDER_SIZE;
        let off_cfdata = offset_cffile + file_hdr_sz;
        let data_block_sz = CFDATA_SIZE + u32::try_from(data.len()).unwrap();
        let sz_cabinet = off_cfdata + data_block_sz;

        let mut out = Vec::new();

        let hdr = CFHeader {
            signature: U32LE::new(CF_SIGNATURE),
            reserved1: U32LE::new(0),
            cbCabinet: U32LE::new(sz_cabinet),
            reserved2: U32LE::new(0),
            coffFiles: U32LE::new(offset_cffile),
            reserved3: U32LE::new(0),
            versionMinor: 3,
            versionMajor: 1,
            cFolders: U16LE::new(1),
            cFiles: U16LE::new(1),
            flags: U16LE::new(0),
            setID: U16LE::new(0),
            iCabinet: U16LE::new(0),
        };
        hdr.write(&mut out).unwrap();

        let folder = CFFolder {
            coffCabStart: U32LE::new(off_cfdata),
            cCFData: U16LE::new(1),
            typeCompress: U16LE::new(u16::from(CFCompressionType::None)),
        };
        folder.write(&mut out).unwrap();

        let fhdr = CFFile {
            cbFile: U32LE::new(data.len().try_into().unwrap()),
            uoffFolderStart: U32LE::new(0),
            iFolder: U16LE::new(0),
            date: 0x5921.into(),
            time: 0x4800.into(),
            attribs: U16LE::new(MsDosFileAttr::Arch.bits()),
        };
        fhdr.write(&mut out).unwrap();
        out.extend_from_slice(filename.as_bytes());
        out.push(0);

        let dh = CFData {
            csum: U32LE::new(0),
            cbData: U16LE::new(data.len().try_into().unwrap()),
            cbUncomp: U16LE::new(data.len().try_into().unwrap()),
        };
        dh.write(&mut out).unwrap();
        out.extend_from_slice(data);

        assert_eq!(out.len(), sz_cabinet as usize);
        out
    }

    fn build_uncompressed_cab_multi(files: &[(&str, &[u8])]) -> Vec<u8> {
        let mut all_data = Vec::new();
        let mut offsets = Vec::new();
        for (_, data) in files {
            offsets.push(all_data.len());
            all_data.extend_from_slice(data);
        }

        let file_hdrs_sz: usize = files
            .iter()
            .map(|(name, _)| CFFILE_SIZE as usize + name.len() + 1)
            .sum();
        let offset_cffile = CFHEADER_SIZE + CFFOLDER_SIZE;
        let off_cfdata = offset_cffile + u32::try_from(file_hdrs_sz).unwrap();
        let data_block_sz = CFDATA_SIZE + u32::try_from(all_data.len()).unwrap();
        let sz_cabinet = off_cfdata + data_block_sz;

        let mut out = Vec::new();

        let hdr = CFHeader {
            signature: U32LE::new(CF_SIGNATURE),
            reserved1: U32LE::new(0),
            cbCabinet: U32LE::new(sz_cabinet),
            reserved2: U32LE::new(0),
            coffFiles: U32LE::new(offset_cffile),
            reserved3: U32LE::new(0),
            versionMinor: 3,
            versionMajor: 1,
            cFolders: U16LE::new(1),
            cFiles: U16LE::new(files.len().try_into().unwrap()),
            flags: U16LE::new(0),
            setID: U16LE::new(0),
            iCabinet: U16LE::new(0),
        };
        hdr.write(&mut out).unwrap();

        let folder = CFFolder {
            coffCabStart: U32LE::new(off_cfdata),
            cCFData: U16LE::new(1),
            typeCompress: U16LE::new(u16::from(CFCompressionType::None)),
        };
        folder.write(&mut out).unwrap();

        for (i, (name, data)) in files.iter().enumerate() {
            let fhdr = CFFile {
                cbFile: U32LE::new(data.len().try_into().unwrap()),
                uoffFolderStart: U32LE::new(offsets[i].try_into().unwrap()),
                iFolder: U16LE::new(0),
                date: 0x5921.into(),
                time: 0x4800.into(),
                attribs: U16LE::new(MsDosFileAttr::Arch.bits()),
            };
            fhdr.write(&mut out).unwrap();
            out.extend_from_slice(name.as_bytes());
            out.push(0);
        }

        let dh = CFData {
            csum: U32LE::new(0),
            cbData: U16LE::new(all_data.len().try_into().unwrap()),
            cbUncomp: U16LE::new(all_data.len().try_into().unwrap()),
        };
        dh.write(&mut out).unwrap();
        out.extend_from_slice(&all_data);

        out
    }

    #[test]
    fn parse_uncompressed_single_file() {
        let content = b"Hello, Cabinet!";
        let cab = build_uncompressed_cab("test.txt", content);
        let mut archive = make_archive(&cab, ParserFlags::empty());
        assert_eq!(archive.files().len(), 1);
        assert_eq!(*archive.files()[0].name(), "test.txt");
        assert_eq!(read_file(&mut archive, 0), content);
    }

    #[test]
    fn parse_uncompressed_preserves_metadata() {
        let cab = build_uncompressed_cab("fw.bin", &[1, 2, 3]);
        let archive = make_archive(&cab, ParserFlags::empty());
        assert_eq!(archive.files()[0].date(), Some(MsDosDate::from(0x5921u16)));
        assert_eq!(archive.files()[0].time(), MsDosTime::from(0x4800u16));
        assert_eq!(
            archive.files()[0].attributes(),
            MsDosFileAttr::Arch.as_mask()
        );
    }

    #[test]
    fn parse_uncompressed_multi_file() {
        let input: &[(&str, &[u8])] = &[
            ("a.txt", b"file A content"),
            ("b.txt", b"file B"),
            ("c.bin", &[0xFF, 0xFE, 0xFD]),
        ];
        let cab = build_uncompressed_cab_multi(input);
        let mut archive = make_archive(&cab, ParserFlags::empty());
        assert_eq!(archive.files().len(), 3);
        assert_eq!(*archive.files()[0].name(), "a.txt");
        assert_eq!(*archive.files()[1].name(), "b.txt");
        assert_eq!(*archive.files()[2].name(), "c.bin");
        assert_eq!(read_file(&mut archive, 0), b"file A content");
        assert_eq!(read_file(&mut archive, 1), b"file B");
        assert_eq!(read_file(&mut archive, 2), &[0xFF, 0xFE, 0xFD]);
    }

    #[test]
    fn parse_uncompressed_empty_file() {
        let cab = build_uncompressed_cab("empty.txt", &[]);
        let mut archive = make_archive(&cab, ParserFlags::empty());
        assert_eq!(*archive.files()[0].name(), "empty.txt");
        assert_eq!(read_file(&mut archive, 0), [] as [u8; 0]);
    }

    #[test]
    fn parse_only_basename() {
        let cab = build_uncompressed_cab("dir/subdir/file.txt", b"data");
        let archive = make_archive(&cab, ParserFlag::OnlyBasename.as_mask());
        assert_eq!(*archive.files()[0].name(), "file.txt");
    }

    #[test]
    fn parse_backslash_path() {
        let cab = build_uncompressed_cab("dir\\file.txt", b"data");
        let archive = make_archive(&cab, ParserFlags::empty());
        assert_eq!(*archive.files()[0].name(), "dir\\file.txt");
    }

    #[test]
    fn parse_rejects_path_traversal() {
        let cab = build_uncompressed_cab("../etc/passwd", b"evil");
        let src = Cursor::new(cab).into_stream();
        assert!(Archive::new(src, 0, ParserFlags::empty()).is_err());
    }

    #[test]
    fn parse_rejects_absolute_path() {
        let cab = build_uncompressed_cab("/etc/passwd", b"evil");
        let src = Cursor::new(cab).into_stream();
        assert!(Archive::new(src, 0, ParserFlags::empty()).is_err());
    }

    #[test]
    fn parse_rejects_duplicate_names() {
        let input: &[(&str, &[u8])] = &[("dup.txt", b"a"), ("dup.txt", b"b")];
        let cab = build_uncompressed_cab_multi(input);
        let src = Cursor::new(cab).into_stream();
        assert!(Archive::new(src, 0, ParserFlags::empty()).is_err());
    }

    #[test]
    fn parse_with_checksum() {
        let content = b"checksum test data";
        let mut cab = build_uncompressed_cab("test.txt", content);

        let off_cfdata = {
            let offset_cffile = u32::from_le_bytes([cab[16], cab[17], cab[18], cab[19]]);
            let file_hdr_sz = CFFILE_SIZE as usize + "test.txt".len() + 1;
            offset_cffile as usize + file_hdr_sz
        };

        let payload_start = off_cfdata + CFDATA_SIZE as usize;
        let sz_compressed: u16 = content.len().try_into().unwrap();
        let sz_uncompressed: u16 = content.len().try_into().unwrap();

        let payload = &cab[payload_start..payload_start + content.len()];
        let mut csum = Checksum::new();
        csum.update(payload);
        let mut hdr_bytes = [0u8; 4];
        hdr_bytes[0..2].copy_from_slice(&sz_compressed.to_le_bytes());
        hdr_bytes[2..4].copy_from_slice(&sz_uncompressed.to_le_bytes());
        csum.update(&hdr_bytes);

        cab[off_cfdata..off_cfdata + 4].copy_from_slice(&csum.checksum().to_le_bytes());

        let mut archive = make_archive(&cab, ParserFlags::empty());
        assert_eq!(read_file(&mut archive, 0), content);
    }

    #[test]
    fn parse_bad_checksum_rejected() {
        let content = b"test data";
        let mut cab = build_uncompressed_cab("test.txt", content);

        let off_cfdata = {
            let offset_cffile = u32::from_le_bytes([cab[16], cab[17], cab[18], cab[19]]);
            let file_hdr_sz = CFFILE_SIZE as usize + "test.txt".len() + 1;
            offset_cffile as usize + file_hdr_sz
        };
        cab[off_cfdata..off_cfdata + 4].copy_from_slice(&0xBAD00BADu32.to_le_bytes());

        let src = Cursor::new(cab).into_stream();
        assert!(Archive::new(src, 0, ParserFlags::empty()).is_err());
    }

    #[test]
    fn parse_bad_checksum_ignored() {
        let content = b"test data";
        let mut cab = build_uncompressed_cab("test.txt", content);

        let off_cfdata = {
            let offset_cffile = u32::from_le_bytes([cab[16], cab[17], cab[18], cab[19]]);
            let file_hdr_sz = CFFILE_SIZE as usize + "test.txt".len() + 1;
            offset_cffile as usize + file_hdr_sz
        };
        cab[off_cfdata..off_cfdata + 4].copy_from_slice(&0xBAD00BADu32.to_le_bytes());

        let mut archive = make_archive(&cab, ParserFlag::IgnoreChecksum.as_mask());
        assert_eq!(read_file(&mut archive, 0), content);
    }

    #[test]
    fn parse_zero_checksum_skipped() {
        let content = b"test data";
        let cab = build_uncompressed_cab("test.txt", content);
        let mut archive = make_archive(&cab, ParserFlags::empty());
        assert_eq!(read_file(&mut archive, 0), content);
    }

    #[test]
    fn parse_truncated_header() {
        let data = [0u8; 10];
        let src = Cursor::new(data).into_stream();
        assert!(Archive::new(src, 0, ParserFlags::empty()).is_err());
    }

    #[test]
    fn parse_truncated_folder() {
        let mut data = vec![0u8; CFHEADER_SIZE as usize + 2];
        data[0..4].copy_from_slice(&CF_SIGNATURE.to_le_bytes());
        data[8..12].copy_from_slice(&(CFHEADER_SIZE + 2).to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&1u16.to_le_bytes());
        data[28..30].copy_from_slice(&1u16.to_le_bytes());
        let src = Cursor::new(data).into_stream();
        assert!(Archive::new(src, 0, ParserFlags::empty()).is_err());
    }

    #[test]
    fn cab_archive_file_name() {
        let cab = build_uncompressed_cab("test.txt", &[1, 2, 3]);
        let archive = make_archive(&cab, ParserFlags::empty());
        assert_eq!(*archive.files()[0].name(), "test.txt");
    }

    #[test]
    fn cab_archive_file_debug() {
        let cab = build_uncompressed_cab("f.txt", &[1]);
        let archive = make_archive(&cab, ParserFlags::empty());
        let s = format!("{:?}", archive.files()[0]);
        assert!(s.contains("FileEntry"));
    }

    #[test]
    fn parse_flags_custom() {
        let flags = ParserFlag::OnlyBasename | ParserFlag::IgnoreChecksum;
        assert!(flags.contains(ParserFlag::OnlyBasename));
        assert!(flags.contains(ParserFlag::IgnoreChecksum));
    }

    #[test]
    fn parse_unsupported_compression() {
        let content = b"test";
        let mut cab = build_uncompressed_cab("test.txt", content);
        let folder_off = CFHEADER_SIZE as usize;
        cab[folder_off + 6..folder_off + 8].copy_from_slice(&0x0003u16.to_le_bytes());
        let src = Cursor::new(cab).into_stream();
        let err = Archive::new(src, 0, ParserFlags::empty()).unwrap_err();
        assert!(matches!(err, CabError::NotSupported(_)));
    }

    #[test]
    fn parse_invalid_folder_idx() {
        let mut cab = build_uncompressed_cab("test.txt", b"data");
        let offset_cffile = u32::from_le_bytes([cab[16], cab[17], cab[18], cab[19]]) as usize;
        cab[offset_cffile + 8..offset_cffile + 10].copy_from_slice(&99u16.to_le_bytes());
        let src = Cursor::new(cab).into_stream();
        assert!(Archive::new(src, 0, ParserFlags::empty()).is_err());
    }
}
