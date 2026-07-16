/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Minimal implementation of a CAB archive file builder. This builder
//! provides exactly enough to add files and create an uncompressed CAB file,
//! nothing more.

use std::num::NonZeroUsize;
use zerocopy::byteorder::little_endian::{U16 as U16LE, U32 as U32LE};

use super::archive::CFString;
use super::path::CabPath;
use super::wire::{
    CFCompressionType, CFData, CFFile, CFFolder, CFHeader, FixedSizeIO, CF_MAJOR_VERSION,
    CF_MINOR_VERSION, CF_SIGNATURE,
};
use super::{Checksum, MsDosDate, MsDosFileAttr, MsDosFileAttrs, MsDosTime};
use crate::compressor::{MszipBlock, MszipCompressor};
use crate::streams::{IntoStream, MemoryInputStream, Stream, StreamImpl};
use crate::units::ByteCount;
use crate::Bitflags;

/// Errors that can occur while building a CAB archive.
#[derive(Debug)]
pub enum BuilderError {
    /// A file with this name was already added twice to the archive.
    DuplicateFilename(String),
    /// A size or count exceeds what the CAB format can represent.
    Limits(String),
    /// Compression failed
    Compression(String),
    /// Writing to the output failed.
    IO(std::io::Error),
}

impl std::fmt::Display for BuilderError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BuilderError::DuplicateFilename(name) => write!(f, "duplicate filename: {name}"),
            BuilderError::Limits(msg) => write!(f, "limit exceeded: {msg}"),
            BuilderError::Compression(msg) => write!(f, "compression failed: {msg}"),
            BuilderError::IO(e) => write!(f, "write failed: {e}"),
        }
    }
}

impl std::error::Error for BuilderError {}

impl From<std::io::Error> for BuilderError {
    fn from(e: std::io::Error) -> Self {
        BuilderError::IO(e)
    }
}

impl From<std::num::TryFromIntError> for BuilderError {
    fn from(e: std::num::TryFromIntError) -> Self {
        BuilderError::Limits(format!("Limit exceeded: {e}"))
    }
}

struct WriterFile {
    path: CabPath,
    uncompressed_size: u32,
    offset_in_folder: u32,
    date: MsDosDate,
    time: MsDosTime,
    attributes: MsDosFileAttrs,
    /// Either the file data or a `MemoryInputStream` with
    /// the compressed data (in `MszipBlock` blocks).
    source: Stream<StreamImpl>,
    compression_type: CFCompressionType,
    compressed_size: u32,
    /// Per-block `(compressed_size, uncompressed_size)`
    block_info: Vec<(usize, usize)>,
}

/// Typestate marker: not built yet
#[doc(hidden)]
pub struct Building;

/// Typestate marker: is built
#[doc(hidden)]
pub struct Built;

/// Helper to build a file to be added to a [`ArchiveBuilder`].
///
/// A file must have a [`CabPath`] and a data source, all
/// other fields are optional.
///
/// Use [`FileBuilder::build`] to finalize the file and
/// pass it to [`ArchiveBuilder::add_file`].
pub struct FileBuilder<State = Building> {
    path: CabPath,
    data_len: u64,
    source: Stream<StreamImpl>,
    date: MsDosDate,
    time: MsDosTime,
    attributes: MsDosFileAttrs,
    compress: bool,
    _state: std::marker::PhantomData<State>,
}

impl FileBuilder<Building> {
    /// Create a new file builder for the given path and the
    /// data source with a known `size`.
    ///
    /// The source may be longer than `size`, at maximum `size` bytes
    /// will be read from the source.
    #[must_use]
    pub fn new(path: CabPath, reader: Stream<StreamImpl>, size: u64) -> Self {
        Self {
            path,
            source: reader,
            data_len: size,
            date: MsDosDate::default(),
            time: MsDosTime::default(),
            attributes: MsDosFileAttrs::empty(),
            compress: false,
            _state: std::marker::PhantomData,
        }
    }

    /// Set the date for this file
    #[must_use]
    pub fn date(mut self, date: MsDosDate) -> Self {
        self.date = date;
        self
    }

    /// Set the timestamp for this file
    #[must_use]
    pub fn time(mut self, time: MsDosTime) -> Self {
        self.time = time;
        self
    }

    /// Set any file attributes on this file
    #[must_use]
    pub fn attributes(mut self, attrs: MsDosFileAttrs) -> Self {
        self.attributes = attrs;
        self
    }

    /// Compress this file
    #[must_use]
    pub fn compress(mut self) -> Self {
        self.compress = true;
        self
    }

    /// Finalize this file so it can  be
    /// passed to [`ArchiveBuilder::add_file`].
    #[must_use]
    pub fn build(self) -> FileBuilder<Built> {
        FileBuilder {
            path: self.path,
            source: self.source,
            data_len: self.data_len,
            date: self.date,
            time: self.time,
            attributes: self.attributes,
            compress: self.compress,
            _state: std::marker::PhantomData,
        }
    }
}

/// Typestate marker: no files have been added yet.
#[doc(hidden)]
pub struct NoFiles;

/// Typestate marker: at least one file has been added.
#[doc(hidden)]
pub struct HasFiles;

/// Builder for creating uncompressed MS Cabinet archives.
///
/// Add files with [`add_file`](ArchiveBuilder::add_file). Once at least one file is
/// present, call [`build`](ArchiveBuilder::build) to validate and serialize
/// the archive into any [`Write`](std::io::Write) target.
///
/// This builder provides only basic functionality, most of the more complex
/// features of CAB files (e.g. splitting files across multiple cab archives) are no
/// longer necessary today.
///
/// This builder does not guarantee any specific layout of the CAB file or
/// that the order of files in the CAB file matches the order of the files
/// being added.
///
/// # Example
///
/// ```no_run
/// use fwupd::cab::{ArchiveBuilder, CabPath, FileBuilder, MsDosDate, MsDosFileAttr, MsDosTime};
/// use fwupd::streams::IntoStream;
/// use fwupd::Bitflags;
/// use std::io::{Cursor, Read};
/// use std::str::FromStr;
///
/// let data = vec![0xDE, 0xAD, 0xBE, 0xEF];
/// let sz = data.len() as u64;
/// let cursor = Cursor::new(data).into_stream();
///
/// let bytes: Vec<u8> = ArchiveBuilder::new()
///     .add_file(
///         FileBuilder::new(
///             CabPath::from_str("firmware.bin").unwrap(),
///             cursor,
///             sz,
///         )
///         .date(MsDosDate::from_ymd(1980, 1, 1))
///         .time(MsDosTime::from_hms(0, 0, 0))
///         .attributes(MsDosFileAttr::Arch.as_mask())
///         .build()
///     )
///     .build(Vec::new())
///     .unwrap();
/// ```
pub struct ArchiveBuilder<State = NoFiles> {
    files: Vec<FileBuilder<Built>>,
    _state: std::marker::PhantomData<State>,
}

impl Default for ArchiveBuilder<NoFiles> {
    fn default() -> Self {
        Self {
            files: Vec::new(),
            _state: std::marker::PhantomData,
        }
    }
}

impl ArchiveBuilder<NoFiles> {
    /// Create a new, empty builder. Add files with [`add_file`](ArchiveBuilder::add_file).
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }
}

impl<S> ArchiveBuilder<S> {
    /// Add a file to the archive.
    ///
    /// Build the file with [`FileBuilder`] and pass the result here.
    /// The reader is consumed lazily during [`build`](ArchiveBuilder::build),
    /// streaming directly to the output without buffering the whole file in memory.
    #[allow(clippy::similar_names)]
    #[must_use]
    pub fn add_file(mut self, file: FileBuilder<Built>) -> ArchiveBuilder<HasFiles> {
        self.files.push(FileBuilder {
            path: file.path,
            source: file.source,
            data_len: file.data_len,
            date: file.date,
            time: file.time,
            attributes: file.attributes,
            compress: file.compress,
            _state: std::marker::PhantomData,
        });
        ArchiveBuilder {
            files: self.files,
            _state: std::marker::PhantomData,
        }
    }
}

impl ArchiveBuilder<HasFiles> {
    /// Append a file to the archive.
    ///
    /// Same as [`ArchiveBuilder::add_file`] but takes a `&mut self`
    /// which makes some callers less awkward.
    #[allow(clippy::similar_names)]
    pub fn append_file(&mut self, file: FileBuilder<Built>) {
        self.files.push(FileBuilder {
            path: file.path,
            source: file.source,
            data_len: file.data_len,
            date: file.date,
            time: file.time,
            attributes: file.attributes,
            compress: file.compress,
            _state: std::marker::PhantomData,
        });
    }

    /// Validate and serialize the CAB archive into `writer`.
    ///
    /// File data is streamed directly to `writer` in 32KB blocks.
    ///
    /// Returns `writer` on success.
    ///
    /// # Example
    /// ```ignore
    /// let filename = "firmware.cab";
    /// let out = std::fs::File::create(filename)
    ///     .map_err(|e| format!("failed to create {filename:?}: {e}"))?;
    /// builder
    ///     .build(std::io::BufWriter::new(out))
    ///     .map_err(|e| format!("build failed: {e}"))?;
    /// ```
    /// # Errors
    ///
    /// Returns [`BuilderError::DuplicateFilename`] if any two files share the
    /// same path, [`BuilderError::Limits`] if a size or count exceeds a built-in
    /// limit, or [`BuilderError::IO`] on write failures.
    #[allow(clippy::similar_names)]
    pub fn build<W: std::io::Write>(self, mut writer: W) -> Result<W, BuilderError> {
        if self.files.len() > super::archive::MAX_FILES {
            return Err(BuilderError::Limits(format!(
                "too many files: {} (max {})",
                self.files.len(),
                super::archive::MAX_FILES
            )));
        }

        let mut seen = std::collections::HashSet::new();
        for file in &self.files {
            let name = file.path.path().to_string_lossy().into_owned();
            if !seen.insert(name.clone()) {
                return Err(BuilderError::DuplicateFilename(name));
            }
        }

        Self::write_archive(self.files, &mut writer)?;
        Ok(writer)
    }

    #[allow(clippy::too_many_lines)]
    fn write_archive(
        files: Vec<FileBuilder<Built>>,
        writer: &mut impl std::io::Write,
    ) -> Result<(), BuilderError> {
        let blocksize: u32 = ByteCount::<u32>::from_kb(32).count();

        // All files go into their own folder. For uncompressed files it
        // doesn't matter much, for compressed files get per-file compression
        // this way which makes the data blocks line up nicer.
        let nfiles: u16 = u16::try_from(files.len())
            .map_err(|_| BuilderError::Limits("too many files (max 65535)".into()))?;
        let nfolders = nfiles;

        // Each header needs to know offsets for the follow-on structs so we need to
        // calculate everything ahead of time before we can write out data.
        //
        // A note on conversions: any u16::try_from()? is already checked earlier and
        // should never happen.
        let mut outfiles: Vec<WriterFile> = Vec::new();
        for file in files {
            let file_size = u32::try_from(file.data_len).map_err(|_| {
                BuilderError::Limits(format!("file '{:?}' exceeds 4 GiB", file.path.path()))
            })?;

            let (source, compression_type, compressed_size, block_info) = if file.compress {
                let mut compressor = MszipCompressor::new(
                    file.source,
                    NonZeroUsize::new(blocksize as usize).unwrap(),
                );
                let blocks = compressor
                    .compress()
                    .map_err(|e| BuilderError::Compression(e.to_string()))?;
                let block_info: Vec<(usize, usize)> = blocks
                    .iter()
                    .map(|b| (b.size(), b.uncompressed_size()))
                    .collect();
                let compressed_size =
                    u32::try_from(blocks.iter().map(MszipBlock::size).sum::<usize>())?;
                let compressed: Vec<u8> = blocks
                    .into_iter()
                    .flat_map(|b| b.as_bytes().to_vec())
                    .collect();
                let source = MemoryInputStream::from_data(compressed).into_stream();
                (
                    source as Stream<StreamImpl>,
                    CFCompressionType::MsZip,
                    compressed_size,
                    block_info,
                )
            } else {
                let mut block_info: Vec<(usize, usize)> = Vec::new();
                let mut remaining = file_size;
                while remaining > 0 {
                    let n = remaining.min(blocksize);
                    block_info.push((n as usize, n as usize));
                    remaining -= n;
                }
                if block_info.is_empty() {
                    // one zero-size block represents an empty file.
                    block_info.push((0, 0));
                }
                (file.source, CFCompressionType::None, file_size, block_info)
            };

            u16::try_from(block_info.len())
                .map_err(|_| BuilderError::Limits("Maximum number of blocks exceeded".into()))?;

            outfiles.push(WriterFile {
                path: file.path.clone(),
                uncompressed_size: file_size,
                offset_in_folder: 0, // Each file has its own folder
                date: file.date,
                time: file.time,
                attributes: file.attributes,
                compression_type,
                compressed_size,
                source,
                block_info,
            });
        }

        let sz_file_headers: usize = outfiles
            .iter()
            .map(|f| CFFile::size_in_bytes() + CFString::from(&f.path).len_including_nul())
            .sum();

        let sz_data_blocks: usize = outfiles
            .iter()
            .map(|f| CFData::size_in_bytes() * f.block_info.len() + f.compressed_size as usize)
            .sum();

        let sz_folder_headers: usize = outfiles.iter().map(|_| CFFolder::size_in_bytes()).sum();

        let sz_cabinet = CFHeader::size_in_bytes()
            .checked_add(sz_folder_headers)
            .and_then(|sz| sz.checked_add(sz_file_headers))
            .and_then(|sz| sz.checked_add(sz_data_blocks))
            .ok_or(BuilderError::Limits("CAB size exceeds usize limits".into()))?;
        let sz_cabinet = u32::try_from(sz_cabinet)
            .map_err(|_| BuilderError::Limits("Maximum CAB size exceeded".into()))?;

        let offset_cffile = u32::try_from(CFHeader::size_in_bytes() + sz_folder_headers)?;

        let cfheader = CFHeader {
            signature: U32LE::new(CF_SIGNATURE),
            reserved1: U32LE::new(0),
            cbCabinet: U32LE::new(sz_cabinet),
            reserved2: U32LE::new(0),
            coffFiles: U32LE::new(offset_cffile),
            reserved3: U32LE::new(0),
            versionMinor: CF_MINOR_VERSION,
            versionMajor: CF_MAJOR_VERSION,
            cFolders: U16LE::new(nfolders),
            cFiles: U16LE::new(nfiles),
            flags: U16LE::new(0),
            setID: U16LE::new(0),
            iCabinet: U16LE::new(0),
        };
        cfheader.write(writer)?;
        // No reserve data needed

        let mut off_cfdata = offset_cffile + u32::try_from(sz_file_headers)?;
        for file in &outfiles {
            let cffolder = CFFolder {
                coffCabStart: U32LE::new(off_cfdata),
                cCFData: U16LE::new(u16::try_from(file.block_info.len())?),
                typeCompress: U16LE::new(file.compression_type.into()),
            };
            cffolder.write(writer)?;
            off_cfdata += u32::try_from(
                CFData::size_in_bytes() * file.block_info.len() + file.compressed_size as usize,
            )?;
        }

        for (folder_index, file) in outfiles.iter().enumerate() {
            let attrs = if file.path.is_ascii() {
                file.attributes
            } else {
                file.attributes | MsDosFileAttr::NameIsUtf
            };
            let name = CFString::from(&file.path);
            let cffile = CFFile {
                cbFile: U32LE::new(file.uncompressed_size),
                uoffFolderStart: U32LE::new(file.offset_in_folder),
                iFolder: U16LE::new(u16::try_from(folder_index)?),
                date: file.date,
                time: file.time,
                attribs: U16LE::new(attrs.bits()),
            };
            cffile.write(writer)?;
            writer.write_all(name.as_bytes_with_nul())?;
        }

        for file in &outfiles {
            for &(compressed_size, uncompressed_size) in &file.block_info {
                if compressed_size == 0 {
                    let cfdata = CFData {
                        csum: U32LE::new(0),
                        cbData: U16LE::new(0),
                        cbUncomp: U16LE::new(0),
                    };
                    cfdata.write(writer)?;
                    continue;
                }

                let data = {
                    let mut buf = vec![0u8; compressed_size];
                    let mut guard = file
                        .source
                        .lock()
                        .map_err(|_| std::io::Error::other("Stream was locked"))?;
                    guard.read_exact(&mut buf)?;
                    buf
                };
                let sz_compressed = u16::try_from(compressed_size)?;
                let sz_uncompressed = u16::try_from(uncompressed_size)?;
                let mut cfdata = CFData {
                    csum: U32LE::new(0),
                    cbData: U16LE::new(sz_compressed),
                    cbUncomp: U16LE::new(sz_uncompressed),
                };

                let mut csum = Checksum::new_from_cfdata(&cfdata);
                csum.update(&data);
                cfdata.csum = U32LE::new(csum.checksum());
                cfdata.write(writer)?;
                writer.write_all(&data)?;
            }
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::super::wire::CF_SIGNATURE;
    use super::*;
    use crate::cab::{Archive, ParserFlag, ParserFlags};
    use crate::streams::{IntoStream, MemoryInputStream};
    use crate::Bitflags;
    use std::io::{Cursor, Read};
    use std::str::FromStr;

    const CFHEADER_SIZE: usize = 36;

    fn build(builder: ArchiveBuilder<HasFiles>) -> Vec<u8> {
        builder.build(Vec::new()).unwrap()
    }

    fn parse(data: Vec<u8>, flags: ParserFlags) -> Archive {
        Archive::new(Cursor::new(data).into_stream(), 0, flags).unwrap()
    }

    fn file_bytes(archive: &mut Archive, idx: usize) -> Vec<u8> {
        let mut bytes = Vec::new();
        archive.files_mut()[idx].read_to_end(&mut bytes).unwrap();
        bytes
    }

    fn make_file(path: &str, data: Vec<u8>) -> FileBuilder<Building> {
        let sz = data.len();
        let stream = MemoryInputStream::from_data(data).into_stream();
        let path = CabPath::from_str(path).unwrap();
        FileBuilder::new(path, stream, sz as u64)
            .date(MsDosDate::from(0x5921u16))
            .time(MsDosTime::from(0x4800u16))
            .attributes(MsDosFileAttr::Arch.as_mask())
    }

    #[test]
    fn build_duplicate_filename_fails() {
        let stream1 = MemoryInputStream::from_data(vec![1]).into_stream();
        let stream2 = MemoryInputStream::from_data(vec![1]).into_stream();
        let path = CabPath::new(&CFString::from("dup.txt")).unwrap();
        let err = ArchiveBuilder::new()
            .add_file(FileBuilder::new(path.clone(), stream1, 1).build())
            .add_file(FileBuilder::new(path, stream2, 1).build())
            .build(Vec::new())
            .unwrap_err();
        assert!(matches!(err, BuilderError::DuplicateFilename(_)));
    }

    #[test]
    #[allow(clippy::similar_names)]
    fn write_single_file() {
        let file = make_file("test.txt", b"Hello!".to_vec());
        ArchiveBuilder::new()
            .add_file(file.build())
            .build(Vec::new())
            .unwrap();
    }

    #[test]
    fn write_roundtrip_single_file() {
        let data = vec![0xDE, 0xAD, 0xBE, 0xEF];
        let file = make_file("firmware.bin", data);
        let cab_data = build(ArchiveBuilder::new().add_file(file.build()));
        let mut archive = parse(cab_data, ParserFlags::empty());
        assert_eq!(*archive.files()[0].name(), "firmware.bin");
        assert_eq!(file_bytes(&mut archive, 0), [0xDE, 0xAD, 0xBE, 0xEF]);
    }

    #[test]
    fn write_roundtrip_multi_file() {
        let cab_data = build(
            ArchiveBuilder::new()
                .add_file(make_file("a.txt", b"File A content here".to_vec()).build())
                .add_file(make_file("b.bin", vec![0xFF; 100]).build()),
        );
        let mut archive = parse(cab_data, ParserFlags::empty());
        assert_eq!(*archive.files()[0].name(), "a.txt");
        assert_eq!(*archive.files()[1].name(), "b.bin");
        assert_eq!(file_bytes(&mut archive, 0), b"File A content here");
        assert_eq!(file_bytes(&mut archive, 1), vec![0xFF; 100]);
    }

    #[test]
    fn write_roundtrip_large_file() {
        let data: Vec<u8> = (0..=255).cycle().take(50000).collect();
        let cab_data =
            build(ArchiveBuilder::new().add_file(make_file("large.bin", data.clone()).build()));
        let mut archive = parse(cab_data, ParserFlags::empty());
        assert_eq!(file_bytes(&mut archive, 0), data);
    }

    #[test]
    fn write_roundtrip_preserves_metadata() {
        let cab_data = build(
            ArchiveBuilder::new().add_file(
                make_file("meta.txt", b"metadata test".to_vec())
                    .date(0x1234.into())
                    .time(0x5678.into())
                    .attributes(MsDosFileAttrs::from_bits(0x0F))
                    .build(),
            ),
        );
        let archive = parse(cab_data, ParserFlags::empty());
        assert_eq!(archive.files()[0].date(), Some(MsDosDate::from(0x1234u16)));
        assert_eq!(archive.files()[0].time(), MsDosTime::from(0x5678u16));
        assert_eq!(
            archive.files()[0].attributes(),
            MsDosFileAttrs::from_bits(0x0F)
        );
    }

    #[test]
    #[allow(clippy::similar_names)]
    fn write_produces_valid_signature() {
        let cab_data = build(ArchiveBuilder::new().add_file(make_file("f.txt", vec![1]).build()));
        let sig = u32::from_le_bytes([cab_data[0], cab_data[1], cab_data[2], cab_data[3]]);
        assert_eq!(sig, CF_SIGNATURE);
    }

    #[test]
    #[allow(clippy::similar_names)]
    fn write_produces_correct_version() {
        let cab_data = build(ArchiveBuilder::new().add_file(make_file("f.txt", vec![1]).build()));
        assert_eq!(cab_data[24], 3); // versionMinor
        assert_eq!(cab_data[25], 1); // versionMajor
    }

    #[test]
    #[allow(clippy::similar_names)]
    fn write_produces_correct_sz_cabinet() {
        let cab_data =
            build(ArchiveBuilder::new().add_file(make_file("f.txt", b"test".to_vec()).build()));
        let stored_sz = u32::from_le_bytes([cab_data[8], cab_data[9], cab_data[10], cab_data[11]]);
        assert_eq!(stored_sz as usize, cab_data.len());
    }

    #[test]
    fn write_roundtrip_empty_file() {
        let cab_data =
            build(ArchiveBuilder::new().add_file(make_file("empty.txt", vec![]).build()));
        let mut archive = parse(cab_data, ParserFlags::empty());
        assert_eq!(*archive.files()[0].name(), "empty.txt");
        assert_eq!(file_bytes(&mut archive, 0), [] as [u8; 0]);
    }

    #[test]
    fn write_roundtrip_with_checksum_verification() {
        let cab_data = build(
            ArchiveBuilder::new().add_file(make_file("checked.bin", vec![0xAA; 500]).build()),
        );
        let mut archive = parse(cab_data, ParserFlags::empty());
        assert_eq!(file_bytes(&mut archive, 0), vec![0xAA; 500]);
    }

    #[test]
    #[allow(clippy::similar_names)]
    fn write_set_id_is_zero() {
        let cab_data = build(ArchiveBuilder::new().add_file(make_file("f.txt", vec![1]).build()));
        let set_id = u16::from_le_bytes([cab_data[32], cab_data[33]]);
        assert_eq!(set_id, 0);
    }

    #[test]
    fn write_one_folder_per_file() {
        let cab_data = build(
            ArchiveBuilder::new()
                .add_file(make_file("a.txt", vec![1, 2, 3]).build())
                .add_file(make_file("b.txt", vec![4, 5, 6]).build()),
        );
        let nr_folders = u16::from_le_bytes([cab_data[26], cab_data[27]]);
        assert_eq!(nr_folders, 2);
    }

    #[test]
    fn write_parse_only_basename() {
        let cab_data = build(
            ArchiveBuilder::new()
                .add_file(make_file("path/to/file.txt", b"content".to_vec()).build()),
        );
        let archive = parse(cab_data, ParserFlag::OnlyBasename.as_mask());
        assert_eq!(*archive.files()[0].name(), "file.txt");
    }

    #[test]
    fn write_roundtrip_multi_block() {
        let data: Vec<u8> = (0..=255).cycle().take(65536).collect();
        let cab_data = build(
            ArchiveBuilder::new().add_file(make_file("multiblock.bin", data.clone()).build()),
        );
        let mut archive = parse(cab_data, ParserFlags::empty());
        let got = file_bytes(&mut archive, 0);
        assert_eq!(got.len(), data.len());
        assert_eq!(got, data);
    }

    #[test]
    fn write_roundtrip_exactly_32k() {
        use crate::units::ByteCount;
        let data = vec![0x42u8; ByteCount::<usize>::from_kb(32).count()];
        let cab_data =
            build(ArchiveBuilder::new().add_file(make_file("exact32k", data.clone()).build()));
        let mut archive = parse(cab_data, ParserFlags::empty());
        assert_eq!(file_bytes(&mut archive, 0), data);
    }

    #[test]
    fn write_roundtrip_32k_plus_1() {
        use crate::units::ByteCount;
        let data = vec![0x42u8; ByteCount::<usize>::from_kb(32).count() + 1];
        let cab_data =
            build(ArchiveBuilder::new().add_file(make_file("32kplus1.bin", data.clone()).build()));
        let mut archive = parse(cab_data, ParserFlags::empty());
        assert_eq!(file_bytes(&mut archive, 0), data);
    }

    #[test]
    fn written_cab_is_valid() {
        build(
            ArchiveBuilder::new()
                .add_file(make_file("x.txt", b"xyz".to_vec()).build())
                .add_file(make_file("y.bin", vec![0u8; 1000]).build()),
        );
    }

    #[test]
    #[allow(clippy::similar_names)]
    fn write_uses_no_compression() {
        let cab_data =
            build(ArchiveBuilder::new().add_file(make_file("f.txt", vec![0; 100]).build()));
        let folder_off = CFHEADER_SIZE;
        let compression_type =
            u16::from_le_bytes([cab_data[folder_off + 6], cab_data[folder_off + 7]]);
        assert_eq!(compression_type, u16::from(CFCompressionType::None));
    }
}
