/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! CAB file parser and writer.
//!
//! This module parses and writes MS Cabinet (`.cab`) archives using a minimal
//! [`ReadAt`] trait for random-access byte sources. This crate requires
//! system libz for MSZIP decompression and compression.
//!
//! The [`ReadAt`] trait must be implemented by the caller to provide a simple
//! random-access read interface with a known size. A blanket implementation
//! is provided for `[u8]`, which means `Vec<u8>` (via `Deref`) and byte slices
//! work out of the box.
//!
//! # Zero-copy for uncompressed archives
//!
//! When parsing an uncompressed CAB archive, file data is not read into
//! memory. Instead, the parser records the byte ranges (spans) within the
//! original source that contain each file's payload. This is represented by
//! [`CabFileData::Deferred`], which stores a list of [`CabSpan`] values.
//!
//! Compressed (MSZIP) data is decompressed eagerly at parse time and stored
//! as [`CabFileData::Owned`].
//!
//! Use [`CabArchiveFile::read_data()`] to materialize deferred data on
//! demand, or inspect the spans directly via [`CabFileData::spans()`] for
//! zero-copy integration (e.g. creating sub-stream views on the C side).
//!
//! # Example
//!
//! ```no_run
//! use fwupd::cab::{CabArchive, CabFileData};
//! use fwupd::firmware::FuFirmwareParseFlags;
//!
//! let data: Vec<u8> = std::fs::read("firmware.cab").unwrap();
//! let archive = CabArchive::parse(&*data, FuFirmwareParseFlags::empty()).unwrap();
//! for file in &archive.files {
//!     match &file.data {
//!         CabFileData::Deferred(spans) => {
//!             // Uncompressed: data is not in memory, use spans to read
//!             // on demand or create zero-copy stream views.
//!             println!("{}: {} spans", file.name, spans.len());
//!             for span in spans {
//!                 println!("  offset=0x{:x}, length={}", span.offset, span.length);
//!             }
//!             // Read the data from the original source when needed:
//!             let bytes = file.read_data(&*data).unwrap();
//!             println!("  {} bytes read", bytes.len());
//!         }
//!         CabFileData::Owned(bytes) => {
//!             // Compressed: data was decompressed at parse time.
//!             println!("{}: {} bytes in memory", file.name, bytes.len());
//!         }
//!     }
//! }
//! ```

use std::fmt;

// ---------------------------------------------------------------------------
// FFI bindings for system zlib
// ---------------------------------------------------------------------------

#[allow(non_camel_case_types)]
mod zlib_ffi {
    use std::os::raw::{c_char, c_int, c_uint, c_ulong};

    pub const Z_OK: c_int = 0;
    pub const Z_STREAM_END: c_int = 1;
    pub const Z_BLOCK: c_int = 5;
    pub const Z_DEFLATED: c_int = 8;
    pub const MAX_WBITS: c_int = 15;

    #[repr(C)]
    pub struct z_stream {
        pub next_in: *mut u8,
        pub avail_in: c_uint,
        pub total_in: c_ulong,
        pub next_out: *mut u8,
        pub avail_out: c_uint,
        pub total_out: c_ulong,
        pub msg: *mut c_char,
        pub state: *mut std::ffi::c_void,
        pub zalloc:
            Option<extern "C" fn(*mut std::ffi::c_void, c_uint, c_uint) -> *mut std::ffi::c_void>,
        pub zfree: Option<extern "C" fn(*mut std::ffi::c_void, *mut std::ffi::c_void)>,
        pub opaque: *mut std::ffi::c_void,
        pub data_type: c_int,
        pub adler: c_ulong,
        pub reserved: c_ulong,
    }

    unsafe impl Send for z_stream {}

    unsafe extern "C" {
        pub fn inflateInit2_(
            strm: *mut z_stream,
            windowBits: c_int,
            version: *const c_char,
            stream_size: c_int,
        ) -> c_int;
        pub fn inflate(strm: *mut z_stream, flush: c_int) -> c_int;
        pub fn inflateReset(strm: *mut z_stream) -> c_int;
        pub fn inflateSetDictionary(
            strm: *mut z_stream,
            dictionary: *const u8,
            dictLength: c_uint,
        ) -> c_int;
        pub fn inflateEnd(strm: *mut z_stream) -> c_int;

        pub fn deflateInit2_(
            strm: *mut z_stream,
            level: c_int,
            method: c_int,
            windowBits: c_int,
            memLevel: c_int,
            strategy: c_int,
            version: *const c_char,
            stream_size: c_int,
        ) -> c_int;
        pub fn deflate(strm: *mut z_stream, flush: c_int) -> c_int;
        pub fn deflateEnd(strm: *mut z_stream) -> c_int;

        pub fn zlibVersion() -> *const c_char;
    }

    pub fn zlib_version() -> *const c_char {
        unsafe { zlibVersion() }
    }

    pub fn new_z_stream() -> z_stream {
        z_stream {
            next_in: std::ptr::null_mut(),
            avail_in: 0,
            total_in: 0,
            next_out: std::ptr::null_mut(),
            avail_out: 0,
            total_out: 0,
            msg: std::ptr::null_mut(),
            state: std::ptr::null_mut(),
            zalloc: None,
            zfree: None,
            opaque: std::ptr::null_mut(),
            data_type: 0,
            adler: 0,
            reserved: 0,
        }
    }
}

// ---------------------------------------------------------------------------
// ReadAt trait
// ---------------------------------------------------------------------------

/// A random-access byte source with a known size.
///
/// Required properties of a byte source are the ability
/// to read from a random offset and to tell the total size
/// of the source.
pub trait ReadAt {
    /// Returns the total size of the data source in bytes.
    fn size(&self) -> usize;

    /// Read up to `buf.len()` bytes starting at `offset`.
    ///
    /// Returns the number of bytes actually read. May return 0 if
    /// `offset` is at or past the end, or if `buf` is empty.
    fn read_at(&self, offset: usize, buf: &mut [u8]) -> Result<usize, CabError>;
}

/// Blanket implementation for byte slices.
///
/// `Vec<u8>` also gets this implementation via `Deref<Target = [u8]>`.
impl ReadAt for [u8] {
    fn size(&self) -> usize {
        self.len()
    }

    fn read_at(&self, offset: usize, buf: &mut [u8]) -> Result<usize, CabError> {
        if offset >= self.len() {
            return Ok(0);
        }
        let available = &self[offset..];
        let n = buf.len().min(available.len());
        buf[..n].copy_from_slice(&available[..n]);
        Ok(n)
    }
}

// ---------------------------------------------------------------------------
// Private ReadAt helpers
// ---------------------------------------------------------------------------

fn read_le_at<T: crate::mem::MemReadWrite>(
    src: &(impl ReadAt + ?Sized),
    offset: usize,
) -> Result<T, CabError> {
    let mut buf = [0u8; 8];
    let buf = &mut buf[..T::SIZE];
    let n = src.read_at(offset, buf)?;
    if n < T::SIZE {
        return Err(CabError::Format(format!(
            "short read at offset 0x{offset:x}: need {} bytes, got {n}",
            T::SIZE
        )));
    }
    Ok(crate::mem::memread::<T>(buf, 0, crate::mem::Endian::Little).unwrap())
}

fn read_bytes_at(
    src: &(impl ReadAt + ?Sized),
    offset: usize,
    count: usize,
) -> Result<Vec<u8>, CabError> {
    let mut buf = vec![0u8; count];
    let n = src.read_at(offset, &mut buf)?;
    if n < count {
        return Err(CabError::Format(format!(
            "short read at offset 0x{offset:x}: need {count} bytes, got {n}"
        )));
    }
    Ok(buf)
}

// ---------------------------------------------------------------------------
// Error type
// ---------------------------------------------------------------------------

/// Errors that can occur during CAB parsing or writing.
#[derive(Debug)]
pub enum CabError {
    /// The data is not a valid MS Cabinet archive or contains
    /// structural errors.
    Format(String),
    /// A feature required to parse the archive is not supported
    /// (e.g. unsupported compression method).
    NotSupported(String),
    /// An error occurred during MSZIP decompression or compression.
    Decompression(String),
    /// A security limit was exceeded (file count, size, etc.).
    Limit(String),
}

impl fmt::Display for CabError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CabError::Format(msg) => write!(f, "CAB format error: {msg}"),
            CabError::NotSupported(msg) => write!(f, "CAB not supported: {msg}"),
            CabError::Decompression(msg) => write!(f, "CAB decompression error: {msg}"),
            CabError::Limit(msg) => write!(f, "CAB limit exceeded: {msg}"),
        }
    }
}

impl std::error::Error for CabError {}

// ---------------------------------------------------------------------------
// Parse flags
// ---------------------------------------------------------------------------

use crate::firmware::FuFirmwareParseFlags;

// ---------------------------------------------------------------------------
// Public archive types
// ---------------------------------------------------------------------------

/// An MS-DOS date packed into a 16-bit value.
///
/// The bit layout is:
///
/// | Bits  | Field        | Range    |
/// |-------|--------------|----------|
/// | 15--9 | year − 1980  | 0--127   |
/// | 8--5  | month        | 1--12    |
/// | 4--0  | day          | 1--31    |
///
/// Use [`From<u16>`] to construct a value and the accessor methods to
/// extract the individual fields.
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct MsDosDate(u16);

impl MsDosDate {
    /// Create a date from individual components.
    ///
    /// `year` is clamped to the representable range 1980--2107.
    pub fn from_ymd(year: u16, month: u8, day: u8) -> Self {
        let y = year.saturating_sub(1980).min(127);
        MsDosDate((y << 9) | ((month as u16 & 0xf) << 5) | (day as u16 & 0x1f))
    }

    /// Returns the year (1980--2107).
    pub fn year(&self) -> u16 {
        (self.0 >> 9) + 1980
    }

    /// Returns the month (1--12).
    pub fn month(&self) -> u8 {
        (self.0 >> 5 & 0xf) as u8
    }

    /// Returns the day of the month (1--31).
    pub fn day(&self) -> u8 {
        (self.0 & 0x1f) as u8
    }
}

impl From<u16> for MsDosDate {
    fn from(v: u16) -> MsDosDate {
        MsDosDate(v)
    }
}

impl From<MsDosDate> for u16 {
    fn from(d: MsDosDate) -> u16 {
        d.0
    }
}

/// An MS-DOS time-of-day packed into a 16-bit value.
///
/// The bit layout is:
///
/// | Bits  | Field      | Range  |
/// |-------|------------|--------|
/// | 15--11| hour       | 0--23  |
/// | 10--5 | minute     | 0--59  |
/// | 4--0  | second / 2 | 0--29  |
///
/// Seconds have 2-second resolution: the stored value is multiplied by
/// two when returned by [`second()`](MsDosTime::second).
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct MsDosTime(u16);

impl MsDosTime {
    /// Create a time from individual components.
    ///
    /// `second` is stored with 2-second resolution (divided by 2).
    pub fn from_hms(hour: u8, minute: u8, second: u8) -> Self {
        MsDosTime(
            ((hour as u16 & 0x1f) << 11)
                | ((minute as u16 & 0x3f) << 5)
                | ((second as u16 / 2) & 0x1f),
        )
    }

    /// Returns the hour (0--23).
    pub fn hour(&self) -> u8 {
        (self.0 >> 11) as u8
    }

    /// Returns the minute (0--59).
    pub fn minute(&self) -> u8 {
        (self.0 >> 5 & 0x3f) as u8
    }

    /// Returns the second (0--58, always even due to 2-second resolution).
    pub fn second(&self) -> u8 {
        ((self.0 & 0x1f) * 2) as u8
    }

    /// Returns the raw packed 16-bit value.
    pub fn packed(&self) -> u16 {
        self.0
    }
}

impl From<u16> for MsDosTime {
    fn from(v: u16) -> MsDosTime {
        MsDosTime(v)
    }
}

impl From<MsDosTime> for u16 {
    fn from(d: MsDosTime) -> u16 {
        d.0
    }
}

bitflags::bitflags! {
    /// MS-DOS / MS Cabinet file attributes stored as a 16-bit bitmask.
    ///
    /// Standard MS-DOS attributes occupy the low byte.  The MS Cabinet
    /// specification adds `EXEC` (bit 6) and `NAME_IS_UTF` (bit 7).
    ///
    /// | Bit | Constant          | Meaning                          |
    /// |-----|-------------------|----------------------------------|
    /// | 0   | `RDONLY`          | read-only                        |
    /// | 1   | `HIDDEN`          | hidden                           |
    /// | 2   | `SYSTEM`          | system file                      |
    /// | 5   | `ARCH`            | modified since last backup        |
    /// | 6   | `EXEC`            | run after extraction (CAB-only)  |
    /// | 7   | `NAME_IS_UTF`     | filename is UTF-8 (CAB-only)     |
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct MsDosFileAttr: u16 {
        /// Read-only (`_A_RDONLY`, bit 0).
        const RDONLY = 0x01;
        /// Hidden (`_A_HIDDEN`, bit 1).
        const HIDDEN = 0x02;
        /// System file (`_A_SYSTEM`, bit 2).
        const SYSTEM = 0x04;
        /// Modified since last backup (`_A_ARCH`, bit 5).
        const ARCH = 0x20;
        /// Run after extraction (`_A_EXEC`, bit 6). CAB-only.
        const EXEC = 0x40;
        /// Filename is UTF-8 (`_A_NAME_IS_UTF`, bit 7). CAB-only.
        const NAME_IS_UTF = 0x80;
    }
}

/// A parsed MS Cabinet archive.
#[derive(Debug, Clone)]
pub struct CabArchive {
    /// The files contained in the archive.
    pub files: Vec<CabArchiveFile>,
    /// Whether the archive data was compressed (MSZIP).
    pub is_compressed: bool,
}

/// A contiguous range within the original data source.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CabSpan {
    /// Byte offset within the source.
    pub offset: usize,
    /// Number of bytes.
    pub length: usize,
}

/// Where a file's data is stored.
///
/// For uncompressed archives, file data is not read during parsing -- instead,
/// the parser records which byte ranges in the original source contain the
/// payload. The data can then be read on demand via
/// [`CabArchiveFile::read_data()`], or the spans can be used directly to
/// create zero-copy stream views (e.g. `FuPartialInputStream` on the C side).
///
/// For compressed archives, data is decompressed at parse time and stored
/// in memory.
#[derive(Debug, Clone, PartialEq)]
pub enum CabFileData {
    /// Data is materialized in memory (compressed archives, or explicitly
    /// constructed files for writing).
    Owned(Vec<u8>),
    /// Data can be read from the original source at these spans.
    /// The spans are contiguous within the source and listed in order;
    /// concatenating them produces the file's uncompressed content.
    Deferred(Vec<CabSpan>),
}

impl CabFileData {
    /// Total size of the file data in bytes.
    pub fn len(&self) -> usize {
        match self {
            CabFileData::Owned(v) => v.len(),
            CabFileData::Deferred(spans) => spans.iter().map(|s| s.length).sum(),
        }
    }

    /// Returns `true` if the file data is empty.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns the data as a slice, if owned.
    ///
    /// For deferred data, returns `None` -- use [`CabArchiveFile::read_data()`]
    /// to read from the source.
    pub fn as_bytes(&self) -> Option<&[u8]> {
        match self {
            CabFileData::Owned(v) => Some(v),
            CabFileData::Deferred(_) => None,
        }
    }

    /// Returns the deferred spans, if any.
    pub fn spans(&self) -> Option<&[CabSpan]> {
        match self {
            CabFileData::Owned(_) => None,
            CabFileData::Deferred(spans) => Some(spans),
        }
    }

    /// Returns `true` if the data is deferred (not yet read from source).
    pub fn is_deferred(&self) -> bool {
        matches!(self, CabFileData::Deferred(_))
    }
}

/// A single file within a [`CabArchive`].
#[derive(Debug, Clone)]
pub struct CabArchiveFile {
    /// The filename (possibly with path separators replaced).
    pub name: String,
    /// The file data -- either owned bytes or deferred spans.
    pub data: CabFileData,
    /// MS-DOS date (packed: `(year-1980)<<9 | month<<5 | day`).
    pub date: MsDosDate,
    /// MS-DOS time (packed: `hour<<11 | minute<<5 | second/2`).
    pub time: MsDosTime,
    /// MS-DOS file attributes.
    pub attributes: MsDosFileAttr,
}

impl CabArchiveFile {
    /// Read the file data from a source, returning owned bytes.
    ///
    /// For owned data, returns a clone of the bytes.
    /// For deferred data, reads the spans from the source and concatenates.
    pub fn read_data(&self, source: &(impl ReadAt + ?Sized)) -> Result<Vec<u8>, CabError> {
        match &self.data {
            CabFileData::Owned(v) => Ok(v.clone()),
            CabFileData::Deferred(spans) => {
                let total: usize = spans.iter().map(|s| s.length).sum();
                let mut buf = Vec::with_capacity(total);
                for span in spans {
                    let chunk = read_bytes_at(source, span.offset, span.length)?;
                    buf.extend_from_slice(&chunk);
                }
                Ok(buf)
            }
        }
    }
}

impl Default for CabArchiveFile {
    fn default() -> Self {
        Self {
            name: String::new(),
            data: CabFileData::Owned(Vec::new()),
            date: 0u16.into(),
            time: 0u16.into(),
            attributes: MsDosFileAttr::empty(),
        }
    }
}

// ---------------------------------------------------------------------------
// MS Cabinet checksum
// ---------------------------------------------------------------------------

/// Compute the MS Cabinet XOR-based checksum.
///
/// The algorithm processes 4 bytes at a time as little-endian u32 values,
/// XORing each into the accumulator. The remainder bytes (1--3) use a
/// specific ordering:
///
/// - 3 bytes: `b[0]<<16 | b[1]<<8 | b[2]`
/// - 2 bytes: `b[0]<<8 | b[1]`
/// - 1 byte:  `b[0]`
pub fn cab_checksum(data: &[u8], seed: u32) -> u32 {
    let mut csum = seed;
    let chunks = data.len() / 4;
    for i in 0..chunks {
        let off = i * 4;
        let val = u32::from_le_bytes([data[off], data[off + 1], data[off + 2], data[off + 3]]);
        csum ^= val;
    }
    let remainder = &data[chunks * 4..];
    match remainder.len() {
        3 => csum ^= (remainder[0] as u32) << 16 | (remainder[1] as u32) << 8 | remainder[2] as u32,
        2 => csum ^= (remainder[0] as u32) << 8 | remainder[1] as u32,
        1 => csum ^= remainder[0] as u32,
        _ => {}
    }
    csum
}

// ---------------------------------------------------------------------------
// Binary structures
// ---------------------------------------------------------------------------

// Cabinet header magic: "MSCF" as little-endian u32
const CAB_SIGNATURE: u32 = 0x4643534D; // 'M','S','C','F'

/// Compression type used in a cabinet folder.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
enum CompressionType {
    /// No compression.
    None = 0x0000,
    /// MSZIP (deflate) compression.
    MsZip = 0x0001,
}

impl CompressionType {
    /// Parse from the raw 16-bit value (low 4 bits only, per the CAB spec).
    fn from_u16(v: u16) -> Result<Self, CabError> {
        match v & 0x000F {
            0x0000 => Ok(CompressionType::None),
            0x0001 => Ok(CompressionType::MsZip),
            other => Err(CabError::NotSupported(format!(
                "unsupported compression type 0x{other:04x}"
            ))),
        }
    }

    /// Return the raw 16-bit value for serialization.
    fn as_u16(self) -> u16 {
        self as u16
    }
}

bitflags::bitflags! {
    /// Cabinet header flags.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    struct CabHeaderFlags: u16 {
        /// Archive has a previous cabinet.
        const HAS_PREV = 0x0001;
        /// Archive has a next cabinet.
        const HAS_NEXT = 0x0002;
        /// Archive has reserved fields.
        const HAS_RESERVE = 0x0004;
    }
}

// Security limits
const MAX_FOLDERS: usize = 64;
const MAX_FILES: usize = 1024;
const MAX_FILE_SIZE: u64 = 0x4_0000_0000; // 16 GB
const MAX_ARCHIVE_SIZE: u64 = 0x4000_0000; // 1 GB
const MAX_RESERVED: usize = 0x10_0000; // 1 MB
const MAX_BLOCK_UNCOMP: usize = 0x8000; // 32 KB

/// MS Cabinet header (36 bytes on disk).
#[derive(Debug, Clone)]
struct CabHeader {
    signature: u32,        // 0x00 "MSCF"
    _reserved1: u32,       // 0x04
    cabinet_sz: u32,       // 0x08
    _reserved2: u32,       // 0x0C
    offset_cffile: u32,    // 0x10
    _reserved3: u32,       // 0x14
    version_minor: u8,     // 0x18
    version_major: u8,     // 0x19
    nr_folders: u16,       // 0x1A
    nr_files: u16,         // 0x1C
    flags: CabHeaderFlags, // 0x1E
    set_id: u16,           // 0x20
    idx_cabinet: u16,      // 0x22
}

const CAB_HEADER_SIZE: usize = 36;

impl CabHeader {
    fn parse(src: &(impl ReadAt + ?Sized), offset: usize) -> Result<Self, CabError> {
        let data = read_bytes_at(src, offset, CAB_HEADER_SIZE)?;
        Ok(Self {
            signature: u32::from_le_bytes([data[0], data[1], data[2], data[3]]),
            _reserved1: u32::from_le_bytes([data[4], data[5], data[6], data[7]]),
            cabinet_sz: u32::from_le_bytes([data[8], data[9], data[10], data[11]]),
            _reserved2: u32::from_le_bytes([data[12], data[13], data[14], data[15]]),
            offset_cffile: u32::from_le_bytes([data[16], data[17], data[18], data[19]]),
            _reserved3: u32::from_le_bytes([data[20], data[21], data[22], data[23]]),
            version_minor: data[24],
            version_major: data[25],
            nr_folders: u16::from_le_bytes([data[26], data[27]]),
            nr_files: u16::from_le_bytes([data[28], data[29]]),
            flags: CabHeaderFlags::from_bits_truncate(u16::from_le_bytes([data[30], data[31]])),
            set_id: u16::from_le_bytes([data[32], data[33]]),
            idx_cabinet: u16::from_le_bytes([data[34], data[35]]),
        })
    }

    fn write(&self, out: &mut Vec<u8>) {
        out.extend_from_slice(&self.signature.to_le_bytes());
        out.extend_from_slice(&self._reserved1.to_le_bytes());
        out.extend_from_slice(&self.cabinet_sz.to_le_bytes());
        out.extend_from_slice(&self._reserved2.to_le_bytes());
        out.extend_from_slice(&self.offset_cffile.to_le_bytes());
        out.extend_from_slice(&self._reserved3.to_le_bytes());
        out.push(self.version_minor);
        out.push(self.version_major);
        out.extend_from_slice(&self.nr_folders.to_le_bytes());
        out.extend_from_slice(&self.nr_files.to_le_bytes());
        out.extend_from_slice(&self.flags.bits().to_le_bytes());
        out.extend_from_slice(&self.set_id.to_le_bytes());
        out.extend_from_slice(&self.idx_cabinet.to_le_bytes());
    }
}

/// Optional reserved-size header (present when `CabHeaderFlags::HAS_RESERVE` is set).
#[derive(Debug, Clone)]
struct CabHeaderReserve {
    sz_header: u16, // bytes of per-cabinet reserved data
    sz_folder: u8,  // bytes of per-folder reserved data
    sz_data: u8,    // bytes of per-data-block reserved data
}

const CAB_HEADER_RESERVE_SIZE: usize = 4;

impl CabHeaderReserve {
    fn parse(src: &(impl ReadAt + ?Sized), offset: usize) -> Result<Self, CabError> {
        let data = read_bytes_at(src, offset, CAB_HEADER_RESERVE_SIZE)?;
        Ok(Self {
            sz_header: u16::from_le_bytes([data[0], data[1]]),
            sz_folder: data[2],
            sz_data: data[3],
        })
    }
}

/// A folder entry (8 bytes on disk, plus per-folder reserved data).
#[derive(Debug, Clone)]
struct CabFolder {
    off_cfdata: u32,                   // offset of first data block
    nr_data_blocks: u16,               // number of data blocks
    compression_type: CompressionType, // compression type
}

const CAB_FOLDER_SIZE: usize = 8;

impl CabFolder {
    fn parse(src: &(impl ReadAt + ?Sized), offset: usize) -> Result<Self, CabError> {
        let data = read_bytes_at(src, offset, CAB_FOLDER_SIZE)?;
        Ok(Self {
            off_cfdata: u32::from_le_bytes([data[0], data[1], data[2], data[3]]),
            nr_data_blocks: u16::from_le_bytes([data[4], data[5]]),
            compression_type: CompressionType::from_u16(u16::from_le_bytes([data[6], data[7]]))?,
        })
    }

    fn write(&self, out: &mut Vec<u8>) {
        out.extend_from_slice(&self.off_cfdata.to_le_bytes());
        out.extend_from_slice(&self.nr_data_blocks.to_le_bytes());
        out.extend_from_slice(&self.compression_type.as_u16().to_le_bytes());
    }
}

/// A file entry header (16 bytes on disk, plus variable-length name).
#[derive(Debug, Clone)]
struct CabFileHeader {
    uncompressed_sz: u32,  // uncompressed size
    off_folder_start: u32, // offset within the uncompressed folder data
    folder_idx: u16,       // folder index
    date: MsDosDate,
    time: MsDosTime,
    attributes: MsDosFileAttr,
    name: String,
}

const CAB_FILE_HEADER_FIXED_SIZE: usize = 16;

impl CabFileHeader {
    fn parse(src: &(impl ReadAt + ?Sized), offset: usize) -> Result<(Self, usize), CabError> {
        let data = read_bytes_at(src, offset, CAB_FILE_HEADER_FIXED_SIZE)?;
        let uncompressed_sz = u32::from_le_bytes([data[0], data[1], data[2], data[3]]);
        let off_folder_start = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
        let folder_idx = u16::from_le_bytes([data[8], data[9]]);
        let date: MsDosDate = u16::from_le_bytes([data[10], data[11]]).into();
        let time: MsDosTime = u16::from_le_bytes([data[12], data[13]]).into();
        let attributes =
            MsDosFileAttr::from_bits_truncate(u16::from_le_bytes([data[14], data[15]]));

        // Read NUL-terminated filename
        let name_offset = offset + CAB_FILE_HEADER_FIXED_SIZE;
        let mut name_bytes = Vec::new();
        let mut pos = name_offset;
        loop {
            let b = read_le_at::<u8>(src, pos)?;
            if b == 0 {
                break;
            }
            name_bytes.push(b);
            pos += 1;
            if name_bytes.len() > 1024 {
                return Err(CabError::Format("filename too long".into()));
            }
        }
        let name = String::from_utf8(name_bytes)
            .map_err(|_| CabError::Format("invalid UTF-8 in filename".into()))?;
        let total_size = CAB_FILE_HEADER_FIXED_SIZE + name.len() + 1; // +1 for NUL

        Ok((
            Self {
                uncompressed_sz,
                off_folder_start,
                folder_idx,
                date,
                time,
                attributes,
                name,
            },
            total_size,
        ))
    }

    fn write(&self, out: &mut Vec<u8>) {
        out.extend_from_slice(&self.uncompressed_sz.to_le_bytes());
        out.extend_from_slice(&self.off_folder_start.to_le_bytes());
        out.extend_from_slice(&self.folder_idx.to_le_bytes());
        out.extend_from_slice(&u16::from(self.date).to_le_bytes());
        out.extend_from_slice(&u16::from(self.time).to_le_bytes());
        out.extend_from_slice(&self.attributes.bits().to_le_bytes());
        out.extend_from_slice(self.name.as_bytes());
        out.push(0); // NUL terminator
    }
}

/// A data block header (8 bytes on disk, plus per-data reserved data).
#[derive(Debug, Clone)]
struct CabData {
    checksum: u32,  // checksum of this data block
    comp_sz: u16,   // compressed size
    uncomp_sz: u16, // uncompressed size
}

const CAB_DATA_HEADER_SIZE: usize = 8;

impl CabData {
    fn parse(src: &(impl ReadAt + ?Sized), offset: usize) -> Result<Self, CabError> {
        let data = read_bytes_at(src, offset, CAB_DATA_HEADER_SIZE)?;
        Ok(Self {
            checksum: u32::from_le_bytes([data[0], data[1], data[2], data[3]]),
            comp_sz: u16::from_le_bytes([data[4], data[5]]),
            uncomp_sz: u16::from_le_bytes([data[6], data[7]]),
        })
    }

    fn write(&self, out: &mut Vec<u8>) {
        out.extend_from_slice(&self.checksum.to_le_bytes());
        out.extend_from_slice(&self.comp_sz.to_le_bytes());
        out.extend_from_slice(&self.uncomp_sz.to_le_bytes());
    }
}

// ---------------------------------------------------------------------------
// MSZIP decompression
// ---------------------------------------------------------------------------

/// MSZIP decompressor wrapping system libz inflate.
///
/// The z_stream is Box-allocated to avoid issues with zlib-ng storing
/// back-pointers into the z_stream struct (which would break if it moves).
struct MszipDecompressor {
    zstrm: Box<zlib_ffi::z_stream>,
    initialized: bool,
    first_block: bool,
}

impl MszipDecompressor {
    fn new() -> Result<Self, CabError> {
        let mut zstrm = Box::new(zlib_ffi::new_z_stream());
        let ret = unsafe {
            zlib_ffi::inflateInit2_(
                &mut *zstrm as *mut _,
                -zlib_ffi::MAX_WBITS,
                zlib_ffi::zlib_version(),
                std::mem::size_of::<zlib_ffi::z_stream>() as i32,
            )
        };
        if ret != zlib_ffi::Z_OK {
            return Err(CabError::Decompression(format!(
                "inflateInit2 failed: {ret}"
            )));
        }
        Ok(Self {
            zstrm,
            initialized: true,
            first_block: true,
        })
    }

    fn decompress(
        &mut self,
        comp_data: &[u8],
        uncomp_sz: usize,
        prev_output: &[u8],
    ) -> Result<Vec<u8>, CabError> {
        if uncomp_sz > MAX_BLOCK_UNCOMP {
            return Err(CabError::Decompression(format!(
                "uncompressed size {uncomp_sz} exceeds maximum {MAX_BLOCK_UNCOMP}"
            )));
        }
        if comp_data.len() < 2 {
            return Err(CabError::Decompression(
                "MSZIP block too short for CK header".into(),
            ));
        }
        // Validate "CK" prefix
        if comp_data[0] != b'C' || comp_data[1] != b'K' {
            return Err(CabError::Decompression(format!(
                "MSZIP block missing CK prefix, got 0x{:02x}{:02x}",
                comp_data[0], comp_data[1]
            )));
        }
        let deflate_data = &comp_data[2..];

        if !self.first_block {
            // Reset and set dictionary from previous output
            let ret = unsafe { zlib_ffi::inflateReset(&mut *self.zstrm as *mut _) };
            if ret != zlib_ffi::Z_OK {
                return Err(CabError::Decompression(format!(
                    "inflateReset failed: {ret}"
                )));
            }
            if !prev_output.is_empty() {
                let ret = unsafe {
                    zlib_ffi::inflateSetDictionary(
                        &mut *self.zstrm as *mut _,
                        prev_output.as_ptr(),
                        prev_output.len() as u32,
                    )
                };
                if ret != zlib_ffi::Z_OK {
                    return Err(CabError::Decompression(format!(
                        "inflateSetDictionary failed: {ret}"
                    )));
                }
            }
        }

        self.first_block = false;

        let mut output = vec![0u8; uncomp_sz];
        // We need a mutable copy of the input pointer since zlib modifies next_in
        let mut input_buf = deflate_data.to_vec();

        self.zstrm.next_in = input_buf.as_mut_ptr();
        self.zstrm.avail_in = input_buf.len() as u32;
        self.zstrm.next_out = output.as_mut_ptr();
        self.zstrm.avail_out = uncomp_sz as u32;

        let ret = unsafe { zlib_ffi::inflate(&mut *self.zstrm as *mut _, zlib_ffi::Z_BLOCK) };
        if ret != zlib_ffi::Z_OK && ret != zlib_ffi::Z_STREAM_END {
            return Err(CabError::Decompression(format!("inflate failed: {ret}")));
        }

        let produced = uncomp_sz - self.zstrm.avail_out as usize;
        if produced != uncomp_sz {
            return Err(CabError::Decompression(format!(
                "inflate produced {produced} bytes, expected {uncomp_sz}"
            )));
        }

        Ok(output)
    }
}

impl Drop for MszipDecompressor {
    fn drop(&mut self) {
        if self.initialized {
            unsafe {
                zlib_ffi::inflateEnd(&mut *self.zstrm as *mut _);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MSZIP compression
// ---------------------------------------------------------------------------

/// Compress a single MSZIP block (with "CK" prefix).
fn mszip_compress_block(input: &[u8]) -> Result<Vec<u8>, CabError> {
    let mut zstrm = Box::new(zlib_ffi::new_z_stream());
    let ret = unsafe {
        zlib_ffi::deflateInit2_(
            &mut *zstrm as *mut _,
            6, // Z_DEFAULT_COMPRESSION equivalent
            zlib_ffi::Z_DEFLATED,
            -zlib_ffi::MAX_WBITS,
            8, // DEF_MEM_LEVEL
            0, // Z_DEFAULT_STRATEGY
            zlib_ffi::zlib_version(),
            std::mem::size_of::<zlib_ffi::z_stream>() as i32,
        )
    };
    if ret != zlib_ffi::Z_OK {
        return Err(CabError::Decompression(format!(
            "deflateInit2 failed: {ret}"
        )));
    }

    // Worst case: input + 12 bytes overhead + CK prefix
    let max_out = input.len() + 512;
    let mut output = vec![0u8; max_out];

    let mut input_buf = input.to_vec();
    zstrm.next_in = input_buf.as_mut_ptr();
    zstrm.avail_in = input_buf.len() as u32;
    zstrm.next_out = output.as_mut_ptr();
    zstrm.avail_out = output.len() as u32;

    // Z_FINISH = 4
    let ret = unsafe { zlib_ffi::deflate(&mut *zstrm as *mut _, 4) };
    let comp_sz = max_out - zstrm.avail_out as usize;

    unsafe {
        zlib_ffi::deflateEnd(&mut *zstrm as *mut _);
    }

    if ret != zlib_ffi::Z_STREAM_END {
        return Err(CabError::Decompression(format!("deflate failed: {ret}")));
    }

    output.truncate(comp_sz);

    // Prepend "CK" header
    let mut result = Vec::with_capacity(2 + comp_sz);
    result.push(b'C');
    result.push(b'K');
    result.extend_from_slice(&output);
    Ok(result)
}

// ---------------------------------------------------------------------------
// Path safety
// ---------------------------------------------------------------------------

fn validate_path(name: &str) -> Result<(), CabError> {
    if name.is_empty() {
        return Err(CabError::Format("empty filename".into()));
    }
    if name.starts_with('/') || name.starts_with('\\') {
        return Err(CabError::Format(format!(
            "absolute path not allowed: {name}"
        )));
    }
    // Check for path traversal
    for component in name.split(['/', '\\']) {
        if component == ".." {
            return Err(CabError::Format(format!(
                "path traversal not allowed: {name}"
            )));
        }
    }
    Ok(())
}

fn sanitize_path(name: &str) -> String {
    // Replace backslashes with forward slashes
    name.replace('\\', "/")
}

fn basename(name: &str) -> String {
    let sanitized = sanitize_path(name);
    match sanitized.rsplit_once('/') {
        Some((_, base)) => base.to_string(),
        None => sanitized,
    }
}

// ---------------------------------------------------------------------------
// Skip NUL-terminated string helper
// ---------------------------------------------------------------------------

fn skip_nul_string(src: &(impl ReadAt + ?Sized), offset: usize) -> Result<usize, CabError> {
    let mut pos = offset;
    loop {
        let b = read_le_at::<u8>(src, pos)?;
        pos += 1;
        if b == 0 {
            return Ok(pos);
        }
        if pos - offset > 4096 {
            return Err(CabError::Format("NUL-terminated string too long".into()));
        }
    }
}

// ---------------------------------------------------------------------------
// CabArchive implementation
// ---------------------------------------------------------------------------

impl CabArchive {
    /// Check if the data starts with the MS Cabinet signature ("MSCF").
    pub fn validate(source: &(impl ReadAt + ?Sized)) -> bool {
        if source.size() < CAB_HEADER_SIZE {
            return false;
        }
        match read_le_at::<u32>(source, 0) {
            Ok(sig) => sig == CAB_SIGNATURE,
            Err(_) => false,
        }
    }

    /// Parse an MS Cabinet archive from a random-access byte source.
    ///
    /// # Errors
    ///
    /// Returns [`CabError`] if the data is not a valid cabinet archive,
    /// if security limits are exceeded, or if decompression fails.
    pub fn parse(
        source: &(impl ReadAt + ?Sized),
        flags: FuFirmwareParseFlags,
    ) -> Result<Self, CabError> {
        let _source_sz = source.size();

        // Parse main header
        let hdr = CabHeader::parse(source, 0)?;

        // Validate signature
        if hdr.signature != CAB_SIGNATURE {
            return Err(CabError::Format(format!(
                "bad signature: 0x{:08x}, expected 0x{:08x}",
                hdr.signature, CAB_SIGNATURE
            )));
        }

        // Version check
        if hdr.version_major != 1 || hdr.version_minor != 3 {
            return Err(CabError::Format(format!(
                "unsupported version {}.{}",
                hdr.version_major, hdr.version_minor
            )));
        }

        // Cabinet size sanity
        let cabinet_sz = hdr.cabinet_sz as u64;
        if cabinet_sz > MAX_FILE_SIZE {
            return Err(CabError::Limit(format!(
                "cabinet size 0x{cabinet_sz:x} exceeds maximum 0x{MAX_FILE_SIZE:x}"
            )));
        }

        // Security limits
        let nr_folders = hdr.nr_folders as usize;
        if nr_folders == 0 {
            return Err(CabError::Format("no folders in cabinet".into()));
        }
        if nr_folders > MAX_FOLDERS {
            return Err(CabError::Limit(format!(
                "too many folders: {nr_folders} (max {MAX_FOLDERS})"
            )));
        }

        let nr_files = hdr.nr_files as usize;
        if nr_files == 0 {
            return Err(CabError::Format("no files in cabinet".into()));
        }
        if nr_files > MAX_FILES {
            return Err(CabError::Limit(format!(
                "too many files: {nr_files} (max {MAX_FILES})"
            )));
        }

        // Process optional reserve header
        let mut off = CAB_HEADER_SIZE;
        let mut rsvd_folder: usize = 0;
        let mut rsvd_data: usize = 0;

        if hdr.flags.contains(CabHeaderFlags::HAS_RESERVE) {
            let reserve = CabHeaderReserve::parse(source, off)?;
            off += CAB_HEADER_RESERVE_SIZE;
            let header = reserve.sz_header as usize;
            rsvd_folder = reserve.sz_folder as usize;
            rsvd_data = reserve.sz_data as usize;

            if header > MAX_RESERVED {
                return Err(CabError::Limit(format!(
                    "reserved header size {header} exceeds maximum {MAX_RESERVED}"
                )));
            }
            // Skip per-cabinet reserved data
            off += header;
        }

        // Skip previous cabinet info
        if hdr.flags.contains(CabHeaderFlags::HAS_PREV) {
            off = skip_nul_string(source, off)?; // previous cabinet name
            off = skip_nul_string(source, off)?; // previous disk name
        }

        // Skip next cabinet info
        if hdr.flags.contains(CabHeaderFlags::HAS_NEXT) {
            off = skip_nul_string(source, off)?; // next cabinet name
            off = skip_nul_string(source, off)?; // next disk name
        }

        // Parse folders
        let mut folders = Vec::with_capacity(nr_folders);
        for _ in 0..nr_folders {
            let folder = CabFolder::parse(source, off)?;
            off += CAB_FOLDER_SIZE + rsvd_folder;
            folders.push(folder);
        }

        // Parse file headers
        off = hdr.offset_cffile as usize;
        let mut file_headers = Vec::with_capacity(nr_files);
        for _ in 0..nr_files {
            let (fhdr, fhdr_sz) = CabFileHeader::parse(source, off)?;
            off += fhdr_sz;
            file_headers.push(fhdr);
        }

        // Determine if archive is compressed
        let is_compressed = folders
            .iter()
            .any(|f| f.compression_type == CompressionType::MsZip);

        // Folder data: either deferred spans (uncompressed) or owned bytes (compressed).
        // Each entry also tracks the total logical size for bounds checking.
        enum FolderData {
            Spans(Vec<CabSpan>, usize), // (spans, total_logical_size)
            Owned(Vec<u8>),
        }

        impl FolderData {
            fn logical_size(&self) -> usize {
                match self {
                    FolderData::Spans(_, sz) => *sz,
                    FolderData::Owned(v) => v.len(),
                }
            }
        }

        let mut folder_data: Vec<FolderData> = Vec::with_capacity(nr_folders);
        for folder in &folders {
            match folder.compression_type {
                CompressionType::None => {
                    // Uncompressed: record source spans, don't copy data
                    let mut spans = Vec::new();
                    let mut total_sz: usize = 0;
                    let mut data_off = folder.off_cfdata as usize;
                    for _ in 0..folder.nr_data_blocks {
                        let dh = CabData::parse(source, data_off)?;
                        data_off += CAB_DATA_HEADER_SIZE + rsvd_data;

                        let uncomp_sz = dh.uncomp_sz as usize;
                        if uncomp_sz > MAX_BLOCK_UNCOMP {
                            return Err(CabError::Limit(format!(
                                "uncompressed block size {uncomp_sz} exceeds {MAX_BLOCK_UNCOMP}"
                            )));
                        }

                        let comp_sz = dh.comp_sz as usize;

                        // Verify checksum if requested (must read data to compute)
                        if !flags.contains(FuFirmwareParseFlags::IGNORE_CHECKSUM)
                            && dh.checksum != 0
                        {
                            let payload = read_bytes_at(source, data_off, comp_sz)?;
                            let csum = cab_checksum(&payload, 0);
                            let mut hdr_bytes = [0u8; 4];
                            hdr_bytes[0..2].copy_from_slice(&dh.comp_sz.to_le_bytes());
                            hdr_bytes[2..4].copy_from_slice(&dh.uncomp_sz.to_le_bytes());
                            let csum = cab_checksum(&hdr_bytes, csum);
                            if csum != dh.checksum {
                                return Err(CabError::Format(format!(
                                    "data block checksum mismatch: computed 0x{csum:08x}, stored 0x{:08x}",
                                    dh.checksum
                                )));
                            }
                        }

                        spans.push(CabSpan {
                            offset: data_off,
                            length: uncomp_sz,
                        });
                        total_sz += uncomp_sz;
                        data_off += comp_sz;
                    }
                    folder_data.push(FolderData::Spans(spans, total_sz));
                }
                CompressionType::MsZip => {
                    let mut decomp = MszipDecompressor::new()?;
                    let mut data = Vec::new();
                    let mut data_off = folder.off_cfdata as usize;
                    let mut prev_block = Vec::new();
                    for _ in 0..folder.nr_data_blocks {
                        let dh = CabData::parse(source, data_off)?;
                        data_off += CAB_DATA_HEADER_SIZE + rsvd_data;

                        let uncomp_sz = dh.uncomp_sz as usize;
                        if uncomp_sz > MAX_BLOCK_UNCOMP {
                            return Err(CabError::Limit(format!(
                                "uncompressed block size {uncomp_sz} exceeds {MAX_BLOCK_UNCOMP}"
                            )));
                        }

                        let comp_sz = dh.comp_sz as usize;
                        let payload = read_bytes_at(source, data_off, comp_sz)?;

                        // Verify checksum if requested
                        if !flags.contains(FuFirmwareParseFlags::IGNORE_CHECKSUM)
                            && dh.checksum != 0
                        {
                            let csum = cab_checksum(&payload, 0);
                            let mut hdr_bytes = [0u8; 4];
                            hdr_bytes[0..2].copy_from_slice(&dh.comp_sz.to_le_bytes());
                            hdr_bytes[2..4].copy_from_slice(&dh.uncomp_sz.to_le_bytes());
                            let csum = cab_checksum(&hdr_bytes, csum);
                            if csum != dh.checksum {
                                return Err(CabError::Format(format!(
                                    "data block checksum mismatch: computed 0x{csum:08x}, stored 0x{:08x}",
                                    dh.checksum
                                )));
                            }
                        }

                        let block = decomp.decompress(&payload, uncomp_sz, &prev_block)?;
                        data.extend_from_slice(&block);
                        prev_block = block;
                        data_off += comp_sz;
                    }
                    folder_data.push(FolderData::Owned(data));
                }
            }
        }

        // Check total uncompressed size
        let total_size: u64 = folder_data.iter().map(|d| d.logical_size() as u64).sum();
        if total_size > MAX_ARCHIVE_SIZE {
            return Err(CabError::Limit(format!(
                "total uncompressed size 0x{total_size:x} exceeds maximum 0x{MAX_ARCHIVE_SIZE:x}"
            )));
        }

        // Extract files from folder data
        let mut files = Vec::with_capacity(nr_files);
        let mut seen_names = std::collections::HashSet::new();

        for fhdr in &file_headers {
            let folder_idx = fhdr.folder_idx as usize;
            if folder_idx >= nr_folders {
                return Err(CabError::Format(format!(
                    "file '{}' references folder {folder_idx}, but only {nr_folders} folders exist",
                    fhdr.name
                )));
            }

            let fdata = &folder_data[folder_idx];
            let start = fhdr.off_folder_start as usize;
            let file_sz = fhdr.uncompressed_sz as usize;

            if start.checked_add(file_sz).is_none() || start + file_sz > fdata.logical_size() {
                return Err(CabError::Format(format!(
                    "file '{}' extends past end of folder data (start=0x{start:x}, size=0x{file_sz:x}, folder_size=0x{:x})",
                    fhdr.name,
                    fdata.logical_size()
                )));
            }

            // Build the file data: owned slice or deferred spans
            let file_data = match fdata {
                FolderData::Owned(bytes) => {
                    CabFileData::Owned(bytes[start..start + file_sz].to_vec())
                }
                FolderData::Spans(folder_spans, _) => {
                    // Map the file's logical [start..start+file_sz] range to
                    // source spans by walking the folder's block spans.
                    let mut file_spans = Vec::new();
                    let mut remaining = file_sz;
                    let mut file_off = start;
                    for span in folder_spans {
                        if remaining == 0 {
                            break;
                        }
                        // Does this span overlap the file's range?
                        if file_off >= span.length {
                            // File starts past this span
                            file_off -= span.length;
                            continue;
                        }
                        let skip = file_off; // bytes to skip within this span
                        let avail = span.length - skip;
                        let take = remaining.min(avail);
                        file_spans.push(CabSpan {
                            offset: span.offset + skip,
                            length: take,
                        });
                        remaining -= take;
                        file_off = 0;
                    }
                    CabFileData::Deferred(file_spans)
                }
            };

            // Sanitize path
            let raw_name = sanitize_path(&fhdr.name);
            validate_path(&raw_name)?;

            let name = if flags.contains(FuFirmwareParseFlags::ONLY_BASENAME) {
                basename(&raw_name)
            } else {
                raw_name
            };

            if name.is_empty() {
                return Err(CabError::Format("empty filename after processing".into()));
            }

            // Reject duplicates
            if !seen_names.insert(name.clone()) {
                return Err(CabError::Format(format!("duplicate filename: {name}")));
            }

            files.push(CabArchiveFile {
                name,
                data: file_data,
                date: fhdr.date,
                time: fhdr.time,
                attributes: fhdr.attributes,
            });
        }

        Ok(Self {
            files,
            is_compressed,
        })
    }

    /// Serialize this archive to a `Vec<u8>`.
    ///
    /// Uses a single folder with `set_id=0`. If `self.is_compressed` is
    /// true, data is compressed in 32 KB chunks with MSZIP; otherwise
    /// data blocks are stored uncompressed. Returns an error if the
    /// archive has no files.
    pub fn write(&self) -> Result<Vec<u8>, CabError> {
        if self.files.is_empty() {
            return Err(CabError::Format("cannot write empty archive".into()));
        }

        // Concatenate all file data
        let mut all_data = Vec::new();
        let mut file_offsets = Vec::new();
        for file in &self.files {
            file_offsets.push(all_data.len() as u32);
            match &file.data {
                CabFileData::Owned(bytes) => all_data.extend_from_slice(bytes),
                CabFileData::Deferred(_) => {
                    return Err(CabError::NotSupported(format!(
                        "cannot write file '{}': data is deferred (not yet read from source)",
                        file.name
                    )));
                }
            }
        }

        // Split into 32KB chunks, compressing if requested
        let mut compressed_blocks: Vec<Vec<u8>> = Vec::new();
        let mut uncomp_sizes: Vec<u16> = Vec::new();
        let mut pos = 0;
        while pos < all_data.len() {
            let end = (pos + MAX_BLOCK_UNCOMP).min(all_data.len());
            let chunk = &all_data[pos..end];
            let block = if self.is_compressed {
                mszip_compress_block(chunk)?
            } else {
                chunk.to_vec()
            };
            uncomp_sizes.push(chunk.len() as u16);
            compressed_blocks.push(block);
            pos = end;
        }
        if all_data.is_empty() {
            // Edge case: files exist but all have zero length.
            // Still need at least one data block.
            let block = if self.is_compressed {
                mszip_compress_block(&[])?
            } else {
                Vec::new()
            };
            uncomp_sizes.push(0);
            compressed_blocks.push(block);
        }
        let nr_data_blocks = compressed_blocks.len() as u16;

        // Calculate file headers size
        let file_headers_size: usize = self
            .files
            .iter()
            .map(|f| CAB_FILE_HEADER_FIXED_SIZE + f.name.len() + 1)
            .sum();

        // Calculate data blocks size
        let data_blocks_size: usize = compressed_blocks
            .iter()
            .map(|b| CAB_DATA_HEADER_SIZE + b.len())
            .sum();

        // Layout:
        // [header 36] [folder 8] [file headers...] [data blocks...]
        let offset_cffile = (CAB_HEADER_SIZE + CAB_FOLDER_SIZE) as u32;
        let off_cfdata = offset_cffile + file_headers_size as u32;
        let cabinet_sz = off_cfdata + data_blocks_size as u32;

        let mut out = Vec::with_capacity(cabinet_sz as usize);

        // Write header
        let hdr = CabHeader {
            signature: CAB_SIGNATURE,
            _reserved1: 0,
            cabinet_sz,
            _reserved2: 0,
            offset_cffile,
            _reserved3: 0,
            version_minor: 3,
            version_major: 1,
            nr_folders: 1,
            nr_files: self.files.len() as u16,
            flags: CabHeaderFlags::empty(),
            set_id: 0,
            idx_cabinet: 0,
        };
        hdr.write(&mut out);

        // Write folder
        let folder = CabFolder {
            off_cfdata,
            nr_data_blocks,
            compression_type: if self.is_compressed {
                CompressionType::MsZip
            } else {
                CompressionType::None
            },
        };
        folder.write(&mut out);

        // Write file headers
        for (i, file) in self.files.iter().enumerate() {
            // Set _A_NAME_IS_UTF for non-ASCII filenames, matching the old
            // C writer behaviour in fu_cab_firmware_write().
            let mut attrs = file.attributes;
            if !file.name.is_ascii() {
                attrs |= MsDosFileAttr::NAME_IS_UTF;
            }
            let fhdr = CabFileHeader {
                uncompressed_sz: file.data.len() as u32, // CabFileData::len()
                off_folder_start: file_offsets[i],
                folder_idx: 0,
                date: file.date,
                time: file.time,
                attributes: attrs,
                name: file.name.clone(),
            };
            fhdr.write(&mut out);
        }

        // Write data blocks
        for (i, comp_data) in compressed_blocks.iter().enumerate() {
            let comp_sz = comp_data.len() as u16;
            let uncomp_sz = uncomp_sizes[i];

            // Compute checksum: payload first, then synthetic header
            let csum = cab_checksum(comp_data, 0);
            let mut hdr_bytes = [0u8; 4];
            hdr_bytes[0..2].copy_from_slice(&comp_sz.to_le_bytes());
            hdr_bytes[2..4].copy_from_slice(&uncomp_sz.to_le_bytes());
            let csum = cab_checksum(&hdr_bytes, csum);

            let dh = CabData {
                checksum: csum,
                comp_sz,
                uncomp_sz,
            };
            dh.write(&mut out);
            out.extend_from_slice(comp_data);
        }

        debug_assert_eq!(out.len(), cabinet_sz as usize);
        Ok(out)
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper: extract file data from a parsed archive, reading deferred
    /// data from `source` if needed.
    fn file_bytes(file: &CabArchiveFile, source: &[u8]) -> Vec<u8> {
        file.read_data(source).unwrap()
    }

    // -----------------------------------------------------------------------
    // ReadAt trait tests
    // -----------------------------------------------------------------------

    #[test]
    fn read_at_slice_basic() {
        let data: &[u8] = &[0x01, 0x02, 0x03, 0x04, 0x05];
        let mut buf = [0u8; 3];
        let n = data.read_at(1, &mut buf).unwrap();
        assert_eq!(n, 3);
        assert_eq!(buf, [0x02, 0x03, 0x04]);
    }

    #[test]
    fn read_at_slice_past_end() {
        let data: &[u8] = &[0x01, 0x02];
        let mut buf = [0u8; 4];
        let n = data.read_at(0, &mut buf).unwrap();
        assert_eq!(n, 2);
        assert_eq!(&buf[..2], &[0x01, 0x02]);
    }

    #[test]
    fn read_at_slice_at_end() {
        let data: &[u8] = &[0x01, 0x02];
        let mut buf = [0u8; 1];
        let n = data.read_at(2, &mut buf).unwrap();
        assert_eq!(n, 0);
    }

    #[test]
    fn read_at_slice_past_end_offset() {
        let data: &[u8] = &[0x01, 0x02];
        let mut buf = [0u8; 1];
        let n = data.read_at(100, &mut buf).unwrap();
        assert_eq!(n, 0);
    }

    #[test]
    fn read_at_empty_slice() {
        let data: &[u8] = &[];
        let mut buf = [0u8; 1];
        let n = data.read_at(0, &mut buf).unwrap();
        assert_eq!(n, 0);
    }

    #[test]
    fn read_at_empty_buf() {
        let data: &[u8] = &[1, 2, 3];
        let mut buf = [0u8; 0];
        let n = data.read_at(0, &mut buf).unwrap();
        assert_eq!(n, 0);
    }

    #[test]
    fn read_at_vec() {
        let data: Vec<u8> = vec![0xAA, 0xBB, 0xCC];
        let mut buf = [0u8; 2];
        let n = (*data).read_at(1, &mut buf).unwrap();
        assert_eq!(n, 2);
        assert_eq!(buf, [0xBB, 0xCC]);
    }

    #[test]
    fn read_at_size() {
        let data: &[u8] = &[1, 2, 3, 4, 5];
        assert_eq!(data.size(), 5);
    }

    #[test]
    fn read_at_vec_size() {
        let data = vec![1u8, 2, 3];
        assert_eq!((*data).size(), 3);
    }

    #[test]
    fn read_at_exact_fit() {
        let data: &[u8] = &[0x10, 0x20, 0x30];
        let mut buf = [0u8; 3];
        let n = data.read_at(0, &mut buf).unwrap();
        assert_eq!(n, 3);
        assert_eq!(buf, [0x10, 0x20, 0x30]);
    }

    // -----------------------------------------------------------------------
    // Helper function tests
    // -----------------------------------------------------------------------

    #[test]
    fn read_u8_at_ok() {
        let data: &[u8] = &[0x42, 0x43];
        assert_eq!(read_le_at::<u8>(data, 0).unwrap(), 0x42);
        assert_eq!(read_le_at::<u8>(data, 1).unwrap(), 0x43);
    }

    #[test]
    fn read_u8_at_oob() {
        let data: &[u8] = &[0x42];
        assert!(read_le_at::<u8>(data, 1).is_err());
    }

    #[test]
    fn read_u16_le_at_ok() {
        let data: &[u8] = &[0x34, 0x12];
        assert_eq!(read_le_at::<u16>(data, 0).unwrap(), 0x1234);
    }

    #[test]
    fn read_u16_le_at_oob() {
        let data: &[u8] = &[0x34];
        assert!(read_le_at::<u16>(data, 0).is_err());
    }

    #[test]
    fn read_u32_le_at_ok() {
        let data: &[u8] = &[0x78, 0x56, 0x34, 0x12];
        assert_eq!(read_le_at::<u32>(data, 0).unwrap(), 0x12345678);
    }

    #[test]
    fn read_u32_le_at_oob() {
        let data: &[u8] = &[0x78, 0x56, 0x34];
        assert!(read_le_at::<u32>(data, 0).is_err());
    }

    #[test]
    fn read_u32_le_at_offset() {
        let data: &[u8] = &[0xFF, 0x78, 0x56, 0x34, 0x12];
        assert_eq!(read_le_at::<u32>(data, 1).unwrap(), 0x12345678);
    }

    #[test]
    fn read_bytes_at_ok() {
        let data: &[u8] = &[0x01, 0x02, 0x03, 0x04];
        let result = read_bytes_at(data, 1, 2).unwrap();
        assert_eq!(result, vec![0x02, 0x03]);
    }

    #[test]
    fn read_bytes_at_oob() {
        let data: &[u8] = &[0x01, 0x02];
        assert!(read_bytes_at(data, 0, 5).is_err());
    }

    #[test]
    fn read_bytes_at_empty() {
        let data: &[u8] = &[0x01, 0x02];
        let result = read_bytes_at(data, 0, 0).unwrap();
        assert!(result.is_empty());
    }

    // -----------------------------------------------------------------------
    // Checksum tests
    // -----------------------------------------------------------------------

    #[test]
    fn checksum_empty() {
        assert_eq!(cab_checksum(&[], 0), 0);
    }

    #[test]
    fn checksum_empty_with_seed() {
        assert_eq!(cab_checksum(&[], 0xDEADBEEF), 0xDEADBEEF);
    }

    #[test]
    fn checksum_4_bytes() {
        let data = [0x01, 0x02, 0x03, 0x04];
        let expected = u32::from_le_bytes(data);
        assert_eq!(cab_checksum(&data, 0), expected);
    }

    #[test]
    fn checksum_8_bytes() {
        let data = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
        let a = u32::from_le_bytes([0x01, 0x02, 0x03, 0x04]);
        let b = u32::from_le_bytes([0x05, 0x06, 0x07, 0x08]);
        assert_eq!(cab_checksum(&data, 0), a ^ b);
    }

    #[test]
    fn checksum_1_remainder() {
        let data = [0x42];
        assert_eq!(cab_checksum(&data, 0), 0x42);
    }

    #[test]
    fn checksum_2_remainder() {
        let data = [0x12, 0x34];
        let expected = (0x12u32 << 8) | 0x34u32;
        assert_eq!(cab_checksum(&data, 0), expected);
    }

    #[test]
    fn checksum_3_remainder() {
        let data = [0xAA, 0xBB, 0xCC];
        let expected = (0xAAu32 << 16) | (0xBBu32 << 8) | 0xCCu32;
        assert_eq!(cab_checksum(&data, 0), expected);
    }

    #[test]
    fn checksum_5_bytes() {
        // 4 bytes processed as u32 LE, then 1 byte remainder
        let data = [0x01, 0x02, 0x03, 0x04, 0x05];
        let word = u32::from_le_bytes([0x01, 0x02, 0x03, 0x04]);
        let remainder = 0x05u32;
        assert_eq!(cab_checksum(&data, 0), word ^ remainder);
    }

    #[test]
    fn checksum_6_bytes() {
        let data = [0x01, 0x02, 0x03, 0x04, 0x10, 0x20];
        let word = u32::from_le_bytes([0x01, 0x02, 0x03, 0x04]);
        let remainder = (0x10u32 << 8) | 0x20u32;
        assert_eq!(cab_checksum(&data, 0), word ^ remainder);
    }

    #[test]
    fn checksum_7_bytes() {
        let data = [0x01, 0x02, 0x03, 0x04, 0xAA, 0xBB, 0xCC];
        let word = u32::from_le_bytes([0x01, 0x02, 0x03, 0x04]);
        let remainder = (0xAAu32 << 16) | (0xBBu32 << 8) | 0xCCu32;
        assert_eq!(cab_checksum(&data, 0), word ^ remainder);
    }

    #[test]
    fn checksum_with_seed() {
        let data = [0x01, 0x02, 0x03, 0x04];
        let word = u32::from_le_bytes(data);
        let seed = 0x12345678u32;
        assert_eq!(cab_checksum(&data, seed), seed ^ word);
    }

    #[test]
    fn checksum_idempotent() {
        let data = [1, 2, 3, 4, 5, 6, 7, 8, 9];
        let c1 = cab_checksum(&data, 0);
        let c2 = cab_checksum(&data, 0);
        assert_eq!(c1, c2);
    }

    #[test]
    fn checksum_two_pass() {
        // Verify two-pass checksum: payload first, then header
        let payload = [0x01, 0x02, 0x03, 0x04];
        let header = [0x05, 0x06, 0x07, 0x08];
        let c1 = cab_checksum(&payload, 0);
        let c2 = cab_checksum(&header, c1);
        // Should be same as XORing all three u32s
        let p = u32::from_le_bytes(payload);
        let h = u32::from_le_bytes(header);
        assert_eq!(c2, p ^ h);
    }

    // -----------------------------------------------------------------------
    // Binary struct parse/write roundtrip tests
    // -----------------------------------------------------------------------

    #[test]
    fn cab_header_parse() {
        let mut buf = vec![0u8; CAB_HEADER_SIZE];
        // signature "MSCF"
        buf[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        // cabinet_sz = 100
        buf[8..12].copy_from_slice(&100u32.to_le_bytes());
        // offset_cffile = 50
        buf[16..20].copy_from_slice(&50u32.to_le_bytes());
        // version
        buf[24] = 3; // minor
        buf[25] = 1; // major
        // nr_folders = 1
        buf[26..28].copy_from_slice(&1u16.to_le_bytes());
        // nr_files = 2
        buf[28..30].copy_from_slice(&2u16.to_le_bytes());

        let hdr = CabHeader::parse(&buf[..], 0).unwrap();
        assert_eq!(hdr.signature, CAB_SIGNATURE);
        assert_eq!(hdr.cabinet_sz, 100);
        assert_eq!(hdr.offset_cffile, 50);
        assert_eq!(hdr.version_minor, 3);
        assert_eq!(hdr.version_major, 1);
        assert_eq!(hdr.nr_folders, 1);
        assert_eq!(hdr.nr_files, 2);
    }

    #[test]
    fn cab_header_roundtrip() {
        let hdr = CabHeader {
            signature: CAB_SIGNATURE,
            _reserved1: 0,
            cabinet_sz: 12345,
            _reserved2: 0,
            offset_cffile: 44,
            _reserved3: 0,
            version_minor: 3,
            version_major: 1,
            nr_folders: 2,
            nr_files: 5,
            flags: CabHeaderFlags::empty(),
            set_id: 42,
            idx_cabinet: 0,
        };
        let mut buf = Vec::new();
        hdr.write(&mut buf);
        assert_eq!(buf.len(), CAB_HEADER_SIZE);

        let parsed = CabHeader::parse(&buf[..], 0).unwrap();
        assert_eq!(parsed.signature, CAB_SIGNATURE);
        assert_eq!(parsed.cabinet_sz, 12345);
        assert_eq!(parsed.offset_cffile, 44);
        assert_eq!(parsed.nr_folders, 2);
        assert_eq!(parsed.nr_files, 5);
        assert_eq!(parsed.set_id, 42);
    }

    #[test]
    fn cab_header_parse_too_short() {
        let buf = [0u8; 10];
        assert!(CabHeader::parse(&buf[..], 0).is_err());
    }

    #[test]
    fn cab_folder_parse() {
        let mut buf = vec![0u8; CAB_FOLDER_SIZE];
        buf[0..4].copy_from_slice(&0x100u32.to_le_bytes());
        buf[4..6].copy_from_slice(&5u16.to_le_bytes());
        buf[6..8].copy_from_slice(&CompressionType::MsZip.as_u16().to_le_bytes());

        let folder = CabFolder::parse(&buf[..], 0).unwrap();
        assert_eq!(folder.off_cfdata, 0x100);
        assert_eq!(folder.nr_data_blocks, 5);
        assert_eq!(folder.compression_type, CompressionType::MsZip);
    }

    #[test]
    fn cab_folder_roundtrip() {
        let folder = CabFolder {
            off_cfdata: 0x200,
            nr_data_blocks: 3,
            compression_type: CompressionType::None,
        };
        let mut buf = Vec::new();
        folder.write(&mut buf);
        assert_eq!(buf.len(), CAB_FOLDER_SIZE);

        let parsed = CabFolder::parse(&buf[..], 0).unwrap();
        assert_eq!(parsed.off_cfdata, 0x200);
        assert_eq!(parsed.nr_data_blocks, 3);
        assert_eq!(parsed.compression_type, CompressionType::None);
    }

    #[test]
    fn cab_data_parse() {
        let mut buf = vec![0u8; CAB_DATA_HEADER_SIZE];
        buf[0..4].copy_from_slice(&0xDEADBEEFu32.to_le_bytes());
        buf[4..6].copy_from_slice(&100u16.to_le_bytes());
        buf[6..8].copy_from_slice(&200u16.to_le_bytes());

        let dh = CabData::parse(&buf[..], 0).unwrap();
        assert_eq!(dh.checksum, 0xDEADBEEF);
        assert_eq!(dh.comp_sz, 100);
        assert_eq!(dh.uncomp_sz, 200);
    }

    #[test]
    fn cab_data_roundtrip() {
        let dh = CabData {
            checksum: 0x12345678,
            comp_sz: 64,
            uncomp_sz: 128,
        };
        let mut buf = Vec::new();
        dh.write(&mut buf);
        assert_eq!(buf.len(), CAB_DATA_HEADER_SIZE);

        let parsed = CabData::parse(&buf[..], 0).unwrap();
        assert_eq!(parsed.checksum, 0x12345678);
        assert_eq!(parsed.comp_sz, 64);
        assert_eq!(parsed.uncomp_sz, 128);
    }

    #[test]
    fn cab_file_header_parse() {
        let mut buf = Vec::new();
        buf.extend_from_slice(&1000u32.to_le_bytes()); // uncompressed_sz
        buf.extend_from_slice(&0u32.to_le_bytes()); // off_folder_start
        buf.extend_from_slice(&0u16.to_le_bytes()); // folder_idx
        buf.extend_from_slice(&0x1234u16.to_le_bytes()); // date
        buf.extend_from_slice(&0x5678u16.to_le_bytes()); // time
        buf.extend_from_slice(&0x20u16.to_le_bytes()); // attributes
        buf.extend_from_slice(b"test.txt\0"); // name

        let (fhdr, sz) = CabFileHeader::parse(&buf[..], 0).unwrap();
        assert_eq!(fhdr.uncompressed_sz, 1000);
        assert_eq!(fhdr.folder_idx, 0);
        assert_eq!(fhdr.date, 0x1234.into());
        assert_eq!(fhdr.time, 0x5678.into());
        assert_eq!(fhdr.attributes, MsDosFileAttr::ARCH);
        assert_eq!(fhdr.name, "test.txt");
        assert_eq!(sz, CAB_FILE_HEADER_FIXED_SIZE + 9); // "test.txt" + NUL
    }

    #[test]
    fn cab_file_header_roundtrip() {
        let fhdr = CabFileHeader {
            uncompressed_sz: 500,
            off_folder_start: 100,
            folder_idx: 0,
            date: 0x5921.into(),
            time: 0x4800.into(),
            attributes: MsDosFileAttr::ARCH,
            name: "firmware.bin".to_string(),
        };
        let mut buf = Vec::new();
        fhdr.write(&mut buf);

        let (parsed, _) = CabFileHeader::parse(&buf[..], 0).unwrap();
        assert_eq!(parsed.uncompressed_sz, 500);
        assert_eq!(parsed.off_folder_start, 100);
        assert_eq!(parsed.folder_idx, 0);
        assert_eq!(parsed.date, 0x5921.into());
        assert_eq!(parsed.time, 0x4800.into());
        assert_eq!(parsed.attributes, MsDosFileAttr::ARCH);
        assert_eq!(parsed.name, "firmware.bin");
    }

    #[test]
    fn cab_header_reserve_parse() {
        let buf: &[u8] = &[0x10, 0x00, 0x04, 0x02];
        let reserve = CabHeaderReserve::parse(buf, 0).unwrap();
        assert_eq!(reserve.sz_header, 16);
        assert_eq!(reserve.sz_folder, 4);
        assert_eq!(reserve.sz_data, 2);
    }

    // -----------------------------------------------------------------------
    // Path validation tests
    // -----------------------------------------------------------------------

    #[test]
    fn validate_path_ok() {
        assert!(validate_path("test.txt").is_ok());
        assert!(validate_path("dir/test.txt").is_ok());
        assert!(validate_path("a/b/c.bin").is_ok());
        assert!(validate_path(".hidden").is_ok());
    }

    #[test]
    fn validate_path_empty() {
        assert!(validate_path("").is_err());
    }

    #[test]
    fn validate_path_absolute() {
        assert!(validate_path("/etc/passwd").is_err());
        assert!(validate_path("\\windows\\system32").is_err());
    }

    #[test]
    fn validate_path_traversal() {
        assert!(validate_path("../etc/passwd").is_err());
        assert!(validate_path("foo/../../bar").is_err());
        assert!(validate_path("..\\windows").is_err());
    }

    #[test]
    fn sanitize_path_backslash() {
        assert_eq!(sanitize_path("dir\\file.txt"), "dir/file.txt");
        assert_eq!(sanitize_path("a\\b\\c"), "a/b/c");
    }

    #[test]
    fn sanitize_path_forward_slash() {
        assert_eq!(sanitize_path("dir/file.txt"), "dir/file.txt");
    }

    #[test]
    fn basename_simple() {
        assert_eq!(basename("file.txt"), "file.txt");
    }

    #[test]
    fn basename_with_dir() {
        assert_eq!(basename("dir/file.txt"), "file.txt");
        assert_eq!(basename("a/b/c/file.bin"), "file.bin");
    }

    #[test]
    fn basename_with_backslash() {
        assert_eq!(basename("dir\\file.txt"), "file.txt");
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

    // -----------------------------------------------------------------------
    // FuFirmwareParseFlags
    // -----------------------------------------------------------------------

    #[test]
    fn parse_flags_none() {
        let flags = FuFirmwareParseFlags::empty();
        assert!(flags.is_empty());
        assert!(!flags.contains(FuFirmwareParseFlags::ONLY_BASENAME));
        assert!(!flags.contains(FuFirmwareParseFlags::IGNORE_CHECKSUM));
    }

    // -----------------------------------------------------------------------
    // Validate tests
    // -----------------------------------------------------------------------

    #[test]
    fn validate_empty() {
        let data: &[u8] = &[];
        assert!(!CabArchive::validate(data));
    }

    #[test]
    fn validate_too_short() {
        let data: &[u8] = &[b'M', b'S', b'C', b'F'];
        assert!(!CabArchive::validate(data));
    }

    #[test]
    fn validate_bad_magic() {
        let data = [0u8; CAB_HEADER_SIZE];
        assert!(!CabArchive::validate(&data[..]));
    }

    #[test]
    fn validate_good_magic() {
        let mut data = [0u8; CAB_HEADER_SIZE];
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        assert!(CabArchive::validate(&data[..]));
    }

    // -----------------------------------------------------------------------
    // Parse error tests
    // -----------------------------------------------------------------------

    #[test]
    fn parse_bad_signature() {
        let data = [0u8; CAB_HEADER_SIZE];
        let flags = FuFirmwareParseFlags::empty();
        let err = CabArchive::parse(&data[..], flags).unwrap_err();
        assert!(matches!(err, CabError::Format(_)));
    }

    #[test]
    fn parse_bad_version() {
        let mut data = [0u8; CAB_HEADER_SIZE];
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        data[24] = 0; // minor
        data[25] = 2; // major (unsupported)
        data[26..28].copy_from_slice(&1u16.to_le_bytes()); // nr_folders
        data[28..30].copy_from_slice(&1u16.to_le_bytes()); // nr_files
        let flags = FuFirmwareParseFlags::empty();
        let err = CabArchive::parse(&data[..], flags).unwrap_err();
        assert!(matches!(err, CabError::Format(_)));
    }

    #[test]
    fn parse_zero_folders() {
        let mut data = [0u8; CAB_HEADER_SIZE];
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&0u16.to_le_bytes()); // 0 folders
        data[28..30].copy_from_slice(&1u16.to_le_bytes());
        let flags = FuFirmwareParseFlags::empty();
        let err = CabArchive::parse(&data[..], flags).unwrap_err();
        assert!(matches!(err, CabError::Format(_)));
    }

    #[test]
    fn parse_zero_files() {
        let mut data = [0u8; CAB_HEADER_SIZE];
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&1u16.to_le_bytes());
        data[28..30].copy_from_slice(&0u16.to_le_bytes()); // 0 files
        let flags = FuFirmwareParseFlags::empty();
        let err = CabArchive::parse(&data[..], flags).unwrap_err();
        assert!(matches!(err, CabError::Format(_)));
    }

    #[test]
    fn parse_too_many_folders() {
        let mut data = [0u8; CAB_HEADER_SIZE];
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&(MAX_FOLDERS as u16 + 1).to_le_bytes());
        data[28..30].copy_from_slice(&1u16.to_le_bytes());
        let flags = FuFirmwareParseFlags::empty();
        let err = CabArchive::parse(&data[..], flags).unwrap_err();
        assert!(matches!(err, CabError::Limit(_)));
    }

    #[test]
    fn parse_too_many_files() {
        let mut data = [0u8; CAB_HEADER_SIZE];
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&1u16.to_le_bytes());
        data[28..30].copy_from_slice(&(MAX_FILES as u16 + 1).to_le_bytes());
        let flags = FuFirmwareParseFlags::empty();
        let err = CabArchive::parse(&data[..], flags).unwrap_err();
        assert!(matches!(err, CabError::Limit(_)));
    }

    // -----------------------------------------------------------------------
    // MSZIP compression/decompression tests
    // -----------------------------------------------------------------------

    #[test]
    fn mszip_compress_empty() {
        let compressed = mszip_compress_block(&[]).unwrap();
        assert!(compressed.len() >= 2);
        assert_eq!(compressed[0], b'C');
        assert_eq!(compressed[1], b'K');
    }

    #[test]
    fn mszip_compress_small() {
        let input = b"Hello, World!";
        let compressed = mszip_compress_block(input).unwrap();
        assert_eq!(compressed[0], b'C');
        assert_eq!(compressed[1], b'K');
        assert!(compressed.len() > 2);
    }

    #[test]
    fn mszip_roundtrip_small() {
        let input = b"Hello, MSZIP compression test!";
        let compressed = mszip_compress_block(input).unwrap();

        let mut decomp = MszipDecompressor::new().unwrap();
        let output = decomp.decompress(&compressed, input.len(), &[]).unwrap();
        assert_eq!(output, input);
    }

    #[test]
    fn mszip_roundtrip_zeros() {
        let input = vec![0u8; 1000];
        let compressed = mszip_compress_block(&input).unwrap();

        let mut decomp = MszipDecompressor::new().unwrap();
        let output = decomp.decompress(&compressed, input.len(), &[]).unwrap();
        assert_eq!(output, input);
    }

    #[test]
    fn mszip_roundtrip_random_pattern() {
        let input: Vec<u8> = (0..=255).cycle().take(4096).collect();
        let compressed = mszip_compress_block(&input).unwrap();

        let mut decomp = MszipDecompressor::new().unwrap();
        let output = decomp.decompress(&compressed, input.len(), &[]).unwrap();
        assert_eq!(output, input);
    }

    #[test]
    fn mszip_decompress_no_ck() {
        let data: &[u8] = &[0x00, 0x00, 0x01, 0x02];
        let mut decomp = MszipDecompressor::new().unwrap();
        let err = decomp.decompress(data, 2, &[]).unwrap_err();
        assert!(matches!(err, CabError::Decompression(_)));
    }

    #[test]
    fn mszip_decompress_too_short() {
        let data: &[u8] = &[b'C'];
        let mut decomp = MszipDecompressor::new().unwrap();
        let err = decomp.decompress(data, 1, &[]).unwrap_err();
        assert!(matches!(err, CabError::Decompression(_)));
    }

    #[test]
    fn mszip_decompress_oversize_block() {
        let data: &[u8] = &[b'C', b'K'];
        let mut decomp = MszipDecompressor::new().unwrap();
        let err = decomp
            .decompress(data, MAX_BLOCK_UNCOMP + 1, &[])
            .unwrap_err();
        assert!(matches!(err, CabError::Decompression(_)));
    }

    #[test]
    fn mszip_roundtrip_32k() {
        let input: Vec<u8> = (0..MAX_BLOCK_UNCOMP).map(|i| (i % 251) as u8).collect();
        let compressed = mszip_compress_block(&input).unwrap();

        let mut decomp = MszipDecompressor::new().unwrap();
        let output = decomp.decompress(&compressed, input.len(), &[]).unwrap();
        assert_eq!(output, input);
    }

    #[test]
    fn mszip_compress_preserves_data() {
        for size in [1, 10, 100, 1000, 8192] {
            let input: Vec<u8> = (0..size).map(|i| (i * 7 + 3) as u8).collect();
            let compressed = mszip_compress_block(&input).unwrap();
            let mut decomp = MszipDecompressor::new().unwrap();
            let output = decomp.decompress(&compressed, input.len(), &[]).unwrap();
            assert_eq!(output, input, "failed for size {size}");
        }
    }

    // -----------------------------------------------------------------------
    // Build a minimal valid cab and parse it
    // -----------------------------------------------------------------------

    /// Build a minimal uncompressed cabinet with one file.
    fn build_uncompressed_cab(filename: &str, data: &[u8]) -> Vec<u8> {
        let file_hdr_sz = CAB_FILE_HEADER_FIXED_SIZE + filename.len() + 1;
        let offset_cffile = (CAB_HEADER_SIZE + CAB_FOLDER_SIZE) as u32;
        let off_cfdata = offset_cffile + file_hdr_sz as u32;
        let data_block_sz = CAB_DATA_HEADER_SIZE + data.len();
        let cabinet_sz = off_cfdata + data_block_sz as u32;

        let mut out = Vec::new();

        // Header
        let hdr = CabHeader {
            signature: CAB_SIGNATURE,
            _reserved1: 0,
            cabinet_sz,
            _reserved2: 0,
            offset_cffile,
            _reserved3: 0,
            version_minor: 3,
            version_major: 1,
            nr_folders: 1,
            nr_files: 1,
            flags: CabHeaderFlags::empty(),
            set_id: 0,
            idx_cabinet: 0,
        };
        hdr.write(&mut out);

        // Folder (uncompressed)
        let folder = CabFolder {
            off_cfdata,
            nr_data_blocks: 1,
            compression_type: CompressionType::None,
        };
        folder.write(&mut out);

        // File header
        let fhdr = CabFileHeader {
            uncompressed_sz: data.len() as u32,
            off_folder_start: 0,
            folder_idx: 0,
            date: 0x5921.into(),
            time: 0x4800.into(),
            attributes: MsDosFileAttr::ARCH,
            name: filename.to_string(),
        };
        fhdr.write(&mut out);

        // Data block (no checksum)
        let dh = CabData {
            checksum: 0,
            comp_sz: data.len() as u16,
            uncomp_sz: data.len() as u16,
        };
        dh.write(&mut out);
        out.extend_from_slice(data);

        assert_eq!(out.len(), cabinet_sz as usize);
        out
    }

    /// Build a minimal uncompressed cabinet with multiple files.
    fn build_uncompressed_cab_multi(files: &[(&str, &[u8])]) -> Vec<u8> {
        // Calculate total uncompressed data
        let mut all_data = Vec::new();
        let mut offsets = Vec::new();
        for (_, data) in files {
            offsets.push(all_data.len());
            all_data.extend_from_slice(data);
        }

        let file_hdrs_sz: usize = files
            .iter()
            .map(|(name, _)| CAB_FILE_HEADER_FIXED_SIZE + name.len() + 1)
            .sum();
        let offset_cffile = (CAB_HEADER_SIZE + CAB_FOLDER_SIZE) as u32;
        let off_cfdata = offset_cffile + file_hdrs_sz as u32;
        let data_block_sz = CAB_DATA_HEADER_SIZE + all_data.len();
        let cabinet_sz = off_cfdata + data_block_sz as u32;

        let mut out = Vec::new();

        let hdr = CabHeader {
            signature: CAB_SIGNATURE,
            _reserved1: 0,
            cabinet_sz,
            _reserved2: 0,
            offset_cffile,
            _reserved3: 0,
            version_minor: 3,
            version_major: 1,
            nr_folders: 1,
            nr_files: files.len() as u16,
            flags: CabHeaderFlags::empty(),
            set_id: 0,
            idx_cabinet: 0,
        };
        hdr.write(&mut out);

        let folder = CabFolder {
            off_cfdata,
            nr_data_blocks: 1,
            compression_type: CompressionType::None,
        };
        folder.write(&mut out);

        for (i, (name, data)) in files.iter().enumerate() {
            let fhdr = CabFileHeader {
                uncompressed_sz: data.len() as u32,
                off_folder_start: offsets[i] as u32,
                folder_idx: 0,
                date: 0x5921.into(),
                time: 0x4800.into(),
                attributes: MsDosFileAttr::ARCH,
                name: name.to_string(),
            };
            fhdr.write(&mut out);
        }

        let dh = CabData {
            checksum: 0,
            comp_sz: all_data.len() as u16,
            uncomp_sz: all_data.len() as u16,
        };
        dh.write(&mut out);
        out.extend_from_slice(&all_data);

        out
    }

    #[test]
    fn parse_uncompressed_single_file() {
        let content = b"Hello, Cabinet!";
        let cab = build_uncompressed_cab("test.txt", content);
        let flags = FuFirmwareParseFlags::empty();
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(archive.files.len(), 1);
        assert_eq!(archive.files[0].name, "test.txt");
        // Uncompressed data is deferred -- not read into memory at parse time
        assert!(archive.files[0].data.is_deferred());
        assert_eq!(file_bytes(&archive.files[0], &cab), content);
        assert!(!archive.is_compressed);
    }

    #[test]
    fn parse_uncompressed_preserves_metadata() {
        let cab = build_uncompressed_cab("fw.bin", &[1, 2, 3]);
        let flags = FuFirmwareParseFlags::empty();
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(archive.files[0].date, 0x5921.into());
        assert_eq!(archive.files[0].time, 0x4800.into());
        assert_eq!(archive.files[0].attributes, MsDosFileAttr::ARCH);
    }

    #[test]
    fn parse_uncompressed_multi_file() {
        let files: &[(&str, &[u8])] = &[
            ("a.txt", b"file A content"),
            ("b.txt", b"file B"),
            ("c.bin", &[0xFF, 0xFE, 0xFD]),
        ];
        let cab = build_uncompressed_cab_multi(files);
        let flags = FuFirmwareParseFlags::empty();
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(archive.files.len(), 3);
        assert_eq!(archive.files[0].name, "a.txt");
        assert_eq!(file_bytes(&archive.files[0], &cab), b"file A content");
        assert_eq!(archive.files[1].name, "b.txt");
        assert_eq!(file_bytes(&archive.files[1], &cab), b"file B");
        assert_eq!(archive.files[2].name, "c.bin");
        assert_eq!(file_bytes(&archive.files[2], &cab), &[0xFF, 0xFE, 0xFD]);
    }

    #[test]
    fn parse_uncompressed_empty_file() {
        let cab = build_uncompressed_cab("empty.txt", &[]);
        let flags = FuFirmwareParseFlags::empty();
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(archive.files[0].name, "empty.txt");
        assert!(archive.files[0].data.is_empty());
    }

    #[test]
    fn parse_only_basename() {
        let cab = build_uncompressed_cab("dir/subdir/file.txt", b"data");
        let flags = FuFirmwareParseFlags::ONLY_BASENAME;
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(archive.files[0].name, "file.txt");
    }

    #[test]
    fn parse_backslash_path() {
        let cab = build_uncompressed_cab("dir\\file.txt", b"data");
        let flags = FuFirmwareParseFlags::empty();
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(archive.files[0].name, "dir/file.txt");
    }

    #[test]
    fn parse_rejects_path_traversal() {
        let cab = build_uncompressed_cab("../etc/passwd", b"evil");
        let flags = FuFirmwareParseFlags::empty();
        assert!(CabArchive::parse(&cab[..], flags).is_err());
    }

    #[test]
    fn parse_rejects_absolute_path() {
        let cab = build_uncompressed_cab("/etc/passwd", b"evil");
        let flags = FuFirmwareParseFlags::empty();
        assert!(CabArchive::parse(&cab[..], flags).is_err());
    }

    #[test]
    fn parse_rejects_duplicate_names() {
        let files: &[(&str, &[u8])] = &[("dup.txt", b"a"), ("dup.txt", b"b")];
        let cab = build_uncompressed_cab_multi(files);
        let flags = FuFirmwareParseFlags::empty();
        assert!(CabArchive::parse(&cab[..], flags).is_err());
    }

    #[test]
    fn parse_with_checksum() {
        // Build cab with valid checksum
        let content = b"checksum test data";
        let mut cab = build_uncompressed_cab("test.txt", content);

        // Compute proper checksum for the data block
        let off_cfdata = {
            let offset_cffile = u32::from_le_bytes([cab[16], cab[17], cab[18], cab[19]]);
            let file_hdr_sz = CAB_FILE_HEADER_FIXED_SIZE + "test.txt".len() + 1;
            offset_cffile as usize + file_hdr_sz
        };

        // The payload starts after the data header
        let payload_start = off_cfdata + CAB_DATA_HEADER_SIZE;
        let comp_sz = content.len() as u16;
        let uncomp_sz = content.len() as u16;

        let payload = &cab[payload_start..payload_start + content.len()];
        let csum = cab_checksum(payload, 0);
        let mut hdr_bytes = [0u8; 4];
        hdr_bytes[0..2].copy_from_slice(&comp_sz.to_le_bytes());
        hdr_bytes[2..4].copy_from_slice(&uncomp_sz.to_le_bytes());
        let csum = cab_checksum(&hdr_bytes, csum);

        // Patch checksum into the data header
        cab[off_cfdata..off_cfdata + 4].copy_from_slice(&csum.to_le_bytes());

        let flags = FuFirmwareParseFlags::empty();
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(file_bytes(&archive.files[0], &cab), content);
    }

    #[test]
    fn parse_bad_checksum_rejected() {
        let content = b"test data";
        let mut cab = build_uncompressed_cab("test.txt", content);

        // Set a bad checksum (non-zero, so it's checked)
        let off_cfdata = {
            let offset_cffile = u32::from_le_bytes([cab[16], cab[17], cab[18], cab[19]]);
            let file_hdr_sz = CAB_FILE_HEADER_FIXED_SIZE + "test.txt".len() + 1;
            offset_cffile as usize + file_hdr_sz
        };
        cab[off_cfdata..off_cfdata + 4].copy_from_slice(&0xBAD00BADu32.to_le_bytes());

        let flags = FuFirmwareParseFlags::empty();
        assert!(CabArchive::parse(&cab[..], flags).is_err());
    }

    #[test]
    fn parse_bad_checksum_ignored() {
        let content = b"test data";
        let mut cab = build_uncompressed_cab("test.txt", content);

        let off_cfdata = {
            let offset_cffile = u32::from_le_bytes([cab[16], cab[17], cab[18], cab[19]]);
            let file_hdr_sz = CAB_FILE_HEADER_FIXED_SIZE + "test.txt".len() + 1;
            offset_cffile as usize + file_hdr_sz
        };
        cab[off_cfdata..off_cfdata + 4].copy_from_slice(&0xBAD00BADu32.to_le_bytes());

        let flags = FuFirmwareParseFlags::IGNORE_CHECKSUM;
        // Should succeed with ignore_checksum
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(file_bytes(&archive.files[0], &cab), content);
    }

    #[test]
    fn parse_zero_checksum_skipped() {
        // Zero checksum should be accepted regardless of ignore_checksum
        let content = b"test data";
        let cab = build_uncompressed_cab("test.txt", content);
        // The build_uncompressed_cab sets checksum to 0
        let flags = FuFirmwareParseFlags::empty();
        let archive = CabArchive::parse(&cab[..], flags).unwrap();
        assert_eq!(file_bytes(&archive.files[0], &cab), content);
    }

    // -----------------------------------------------------------------------
    // Write tests
    // -----------------------------------------------------------------------

    #[test]
    fn write_empty_archive_fails() {
        let archive = CabArchive {
            files: vec![],
            is_compressed: false,
        };
        assert!(archive.write().is_err());
    }

    #[test]
    fn write_single_file() {
        let archive = CabArchive {
            files: vec![CabArchiveFile {
                name: "test.txt".to_string(),
                data: CabFileData::Owned(b"Hello!".to_vec()),
                date: 0x5921.into(),
                time: 0x4800.into(),
                attributes: MsDosFileAttr::ARCH,
            }],
            is_compressed: false,
        };
        let cab_data = archive.write().unwrap();
        assert!(CabArchive::validate(&cab_data[..]));
    }

    #[test]
    fn write_roundtrip_single_file() {
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "firmware.bin".to_string(),
                data: CabFileData::Owned(vec![0xDE, 0xAD, 0xBE, 0xEF]),
                date: 0x5921.into(),
                time: 0x4800.into(),
                attributes: MsDosFileAttr::ARCH,
            }],
            is_compressed: true,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(parsed.files.len(), 1);
        assert_eq!(parsed.files[0].name, "firmware.bin");
        // Compressed data is owned -- decompressed at parse time
        assert!(!parsed.files[0].data.is_deferred());
        assert_eq!(
            file_bytes(&parsed.files[0], &cab_data),
            [0xDE, 0xAD, 0xBE, 0xEF]
        );
        assert!(parsed.is_compressed);
    }

    #[test]
    fn write_roundtrip_multi_file() {
        let original = CabArchive {
            files: vec![
                CabArchiveFile {
                    name: "a.txt".to_string(),
                    data: CabFileData::Owned(b"File A content here".to_vec()),
                    date: 0x5921.into(),
                    time: 0x4800.into(),
                    attributes: MsDosFileAttr::ARCH,
                },
                CabArchiveFile {
                    name: "b.bin".to_string(),
                    data: CabFileData::Owned(vec![0xFF; 100]),
                    date: 0x5922.into(),
                    time: 0x4801.into(),
                    attributes: MsDosFileAttr::RDONLY,
                },
            ],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(parsed.files.len(), 2);
        assert_eq!(parsed.files[0].name, "a.txt");
        assert_eq!(
            file_bytes(&parsed.files[0], &cab_data),
            b"File A content here"
        );
        assert_eq!(parsed.files[1].name, "b.bin");
        assert_eq!(file_bytes(&parsed.files[1], &cab_data), vec![0xFF; 100]);
    }

    #[test]
    fn write_roundtrip_large_file() {
        let data: Vec<u8> = (0..=255).cycle().take(50000).collect();
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "large.bin".to_string(),
                data: CabFileData::Owned(data.clone()),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(file_bytes(&parsed.files[0], &cab_data), data);
    }

    #[test]
    fn write_roundtrip_preserves_metadata() {
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "meta.txt".to_string(),
                data: CabFileData::Owned(b"metadata test".to_vec()),
                date: 0x1234.into(),
                time: 0x5678.into(),
                attributes: MsDosFileAttr::from_bits_truncate(0x0F),
            }],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(parsed.files[0].date, 0x1234.into());
        assert_eq!(parsed.files[0].time, 0x5678.into());
        assert_eq!(
            parsed.files[0].attributes,
            MsDosFileAttr::from_bits_truncate(0x0F)
        );
    }

    #[test]
    fn write_produces_valid_signature() {
        let archive = CabArchive {
            files: vec![CabArchiveFile {
                name: "f.txt".to_string(),
                data: CabFileData::Owned(vec![1]),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = archive.write().unwrap();
        let sig = u32::from_le_bytes([cab_data[0], cab_data[1], cab_data[2], cab_data[3]]);
        assert_eq!(sig, CAB_SIGNATURE);
    }

    #[test]
    fn write_produces_correct_version() {
        let archive = CabArchive {
            files: vec![CabArchiveFile {
                name: "f.txt".to_string(),
                data: CabFileData::Owned(vec![1]),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = archive.write().unwrap();
        assert_eq!(cab_data[24], 3); // minor
        assert_eq!(cab_data[25], 1); // major
    }

    #[test]
    fn write_produces_correct_cabinet_sz() {
        let archive = CabArchive {
            files: vec![CabArchiveFile {
                name: "f.txt".to_string(),
                data: CabFileData::Owned(b"test".to_vec()),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = archive.write().unwrap();
        let stored_sz = u32::from_le_bytes([cab_data[8], cab_data[9], cab_data[10], cab_data[11]]);
        assert_eq!(stored_sz as usize, cab_data.len());
    }

    #[test]
    fn write_roundtrip_empty_file() {
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "empty.txt".to_string(),
                data: CabFileData::Owned(vec![]),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(parsed.files[0].name, "empty.txt");
        assert!(parsed.files[0].data.is_empty());
    }

    #[test]
    fn write_roundtrip_with_checksum_verification() {
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "checked.bin".to_string(),
                data: CabFileData::Owned(vec![0xAA; 500]),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        // Parse with checksum verification enabled
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(file_bytes(&parsed.files[0], &cab_data), vec![0xAA; 500]);
    }

    #[test]
    fn write_uses_mszip() {
        let archive = CabArchive {
            files: vec![CabArchiveFile {
                name: "f.txt".to_string(),
                data: CabFileData::Owned(vec![0; 100]),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: true,
        };
        let cab_data = archive.write().unwrap();
        // Read the folder's compression_type
        let folder_off = CAB_HEADER_SIZE;
        let compression_type =
            u16::from_le_bytes([cab_data[folder_off + 6], cab_data[folder_off + 7]]);
        assert_eq!(compression_type, CompressionType::MsZip.as_u16());
    }

    #[test]
    fn write_set_id_is_zero() {
        let archive = CabArchive {
            files: vec![CabArchiveFile {
                name: "f.txt".to_string(),
                data: CabFileData::Owned(vec![1]),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = archive.write().unwrap();
        let set_id = u16::from_le_bytes([cab_data[32], cab_data[33]]);
        assert_eq!(set_id, 0);
    }

    #[test]
    fn write_one_folder() {
        let archive = CabArchive {
            files: vec![
                CabArchiveFile {
                    name: "a.txt".to_string(),
                    data: CabFileData::Owned(vec![1, 2, 3]),
                    date: 0.into(),
                    time: 0.into(),
                    attributes: MsDosFileAttr::empty(),
                },
                CabArchiveFile {
                    name: "b.txt".to_string(),
                    data: CabFileData::Owned(vec![4, 5, 6]),
                    date: 0.into(),
                    time: 0.into(),
                    attributes: MsDosFileAttr::empty(),
                },
            ],
            is_compressed: false,
        };
        let cab_data = archive.write().unwrap();
        let nr_folders = u16::from_le_bytes([cab_data[26], cab_data[27]]);
        assert_eq!(nr_folders, 1);
    }

    // -----------------------------------------------------------------------
    // Integration: write then parse with only_basename
    // -----------------------------------------------------------------------

    #[test]
    fn write_parse_only_basename() {
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "path/to/file.txt".to_string(),
                data: CabFileData::Owned(b"content".to_vec()),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::ONLY_BASENAME;
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(parsed.files[0].name, "file.txt");
        assert_eq!(file_bytes(&parsed.files[0], &cab_data), b"content");
    }

    // -----------------------------------------------------------------------
    // Edge case tests
    // -----------------------------------------------------------------------

    #[test]
    fn parse_truncated_header() {
        let data = [0u8; 10];
        let flags = FuFirmwareParseFlags::empty();
        assert!(CabArchive::parse(&data[..], flags).is_err());
    }

    #[test]
    fn parse_truncated_folder() {
        // Valid header but folder data is truncated
        let mut data = vec![0u8; CAB_HEADER_SIZE + 2]; // Not enough for folder
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        data[8..12].copy_from_slice(&((CAB_HEADER_SIZE + 2) as u32).to_le_bytes());
        data[24] = 3;
        data[25] = 1;
        data[26..28].copy_from_slice(&1u16.to_le_bytes());
        data[28..30].copy_from_slice(&1u16.to_le_bytes());
        let flags = FuFirmwareParseFlags::empty();
        assert!(CabArchive::parse(&data[..], flags).is_err());
    }

    #[test]
    fn validate_with_vec() {
        let mut data = vec![0u8; CAB_HEADER_SIZE];
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        assert!(CabArchive::validate(&*data));
    }

    #[test]
    fn cab_archive_file_clone() {
        let file = CabArchiveFile {
            name: "test.txt".to_string(),
            data: CabFileData::Owned(vec![1, 2, 3]),
            date: 0x5921.into(),
            time: 0x4800.into(),
            attributes: MsDosFileAttr::ARCH,
        };
        let cloned = file.clone();
        assert_eq!(cloned.name, file.name);
        assert_eq!(cloned.data, file.data);
    }

    #[test]
    fn cab_archive_clone() {
        let archive = CabArchive {
            files: vec![CabArchiveFile {
                name: "f.txt".to_string(),
                data: CabFileData::Owned(vec![1]),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: true,
        };
        let cloned = archive.clone();
        assert_eq!(cloned.files.len(), 1);
        assert_eq!(cloned.is_compressed, true);
    }

    #[test]
    fn cab_archive_debug() {
        let archive = CabArchive {
            files: vec![],
            is_compressed: false,
        };
        let s = format!("{archive:?}");
        assert!(s.contains("CabArchive"));
    }

    #[test]
    fn parse_flags_custom() {
        let flags = FuFirmwareParseFlags::ONLY_BASENAME | FuFirmwareParseFlags::IGNORE_CHECKSUM;
        assert!(flags.contains(FuFirmwareParseFlags::ONLY_BASENAME));
        assert!(flags.contains(FuFirmwareParseFlags::IGNORE_CHECKSUM));
    }

    // -----------------------------------------------------------------------
    // Multi-block compression tests
    // -----------------------------------------------------------------------

    #[test]
    fn write_roundtrip_multi_block() {
        // Create data larger than 32KB to force multiple blocks
        let data: Vec<u8> = (0..=255).cycle().take(65536).collect();
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "multiblock.bin".to_string(),
                data: CabFileData::Owned(data.clone()),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(parsed.files[0].data.len(), data.len());
        assert_eq!(file_bytes(&parsed.files[0], &cab_data), data);
    }

    #[test]
    fn write_roundtrip_exactly_32k() {
        let data = vec![0x42u8; MAX_BLOCK_UNCOMP];
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "exact32k.bin".to_string(),
                data: CabFileData::Owned(data.clone()),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(file_bytes(&parsed.files[0], &cab_data), data);
    }

    #[test]
    fn write_roundtrip_32k_plus_1() {
        let data = vec![0x42u8; MAX_BLOCK_UNCOMP + 1];
        let original = CabArchive {
            files: vec![CabArchiveFile {
                name: "32kplus1.bin".to_string(),
                data: CabFileData::Owned(data.clone()),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: false,
        };
        let cab_data = original.write().unwrap();
        let flags = FuFirmwareParseFlags::empty();
        let parsed = CabArchive::parse(&cab_data[..], flags).unwrap();
        assert_eq!(file_bytes(&parsed.files[0], &cab_data), data);
    }

    // -----------------------------------------------------------------------
    // Unsupported compression type
    // -----------------------------------------------------------------------

    #[test]
    fn parse_unsupported_compression() {
        // Build a cab with LZX compression type (0x0003)
        let content = b"test";
        let mut cab = build_uncompressed_cab("test.txt", content);
        // Patch the folder compression type
        let folder_off = CAB_HEADER_SIZE;
        cab[folder_off + 6..folder_off + 8].copy_from_slice(&0x0003u16.to_le_bytes());
        let flags = FuFirmwareParseFlags::empty();
        let err = CabArchive::parse(&cab[..], flags).unwrap_err();
        assert!(matches!(err, CabError::NotSupported(_)));
    }

    // -----------------------------------------------------------------------
    // File referencing invalid folder
    // -----------------------------------------------------------------------

    #[test]
    fn parse_invalid_folder_idx() {
        let mut cab = build_uncompressed_cab("test.txt", b"data");
        // Patch folder_idx in the file header to point to non-existent folder
        let offset_cffile = u32::from_le_bytes([cab[16], cab[17], cab[18], cab[19]]) as usize;
        cab[offset_cffile + 8..offset_cffile + 10].copy_from_slice(&99u16.to_le_bytes());
        let flags = FuFirmwareParseFlags::empty();
        assert!(CabArchive::parse(&cab[..], flags).is_err());
    }

    // -----------------------------------------------------------------------
    // Verify write output can be validated
    // -----------------------------------------------------------------------

    #[test]
    fn written_cab_is_valid() {
        let archive = CabArchive {
            files: vec![
                CabArchiveFile {
                    name: "x.txt".to_string(),
                    data: CabFileData::Owned(b"xyz".to_vec()),
                    date: 0.into(),
                    time: 0.into(),
                    attributes: MsDosFileAttr::empty(),
                },
                CabArchiveFile {
                    name: "y.bin".to_string(),
                    data: CabFileData::Owned(vec![0u8; 1000]),
                    date: 0.into(),
                    time: 0.into(),
                    attributes: MsDosFileAttr::empty(),
                },
            ],
            is_compressed: false,
        };
        let cab_data = archive.write().unwrap();
        assert!(CabArchive::validate(&cab_data[..]));
    }

    // -----------------------------------------------------------------------
    // NUL string skip helper
    // -----------------------------------------------------------------------

    #[test]
    fn skip_nul_string_basic() {
        let data: &[u8] = &[b'h', b'i', 0, b'x'];
        let pos = skip_nul_string(data, 0).unwrap();
        assert_eq!(pos, 3);
    }

    #[test]
    fn skip_nul_string_at_offset() {
        let data: &[u8] = &[0xFF, b'a', b'b', 0, b'x'];
        let pos = skip_nul_string(data, 1).unwrap();
        assert_eq!(pos, 4);
    }

    #[test]
    fn skip_nul_string_empty() {
        let data: &[u8] = &[0];
        let pos = skip_nul_string(data, 0).unwrap();
        assert_eq!(pos, 1);
    }

    // -----------------------------------------------------------------------
    // Compression ratio sanity
    // -----------------------------------------------------------------------

    #[test]
    fn compressed_cab_smaller_than_uncompressed_for_repetitive() {
        let data = vec![0x42u8; 10000];
        let archive = CabArchive {
            files: vec![CabArchiveFile {
                name: "repetitive.bin".to_string(),
                data: CabFileData::Owned(data.clone()),
                date: 0.into(),
                time: 0.into(),
                attributes: MsDosFileAttr::empty(),
            }],
            is_compressed: true,
        };
        let cab_data = archive.write().unwrap();
        // Compressed cab should be smaller than uncompressed data + overhead
        assert!(cab_data.len() < data.len());
    }

    // -----------------------------------------------------------------------
    // ReadAt with dyn trait object
    // -----------------------------------------------------------------------

    #[test]
    fn read_at_generic_dispatch() {
        let data: &[u8] = &[10, 20, 30, 40, 50];
        assert_eq!(data.size(), 5);
        let mut buf = [0u8; 2];
        let n = data.read_at(2, &mut buf).unwrap();
        assert_eq!(n, 2);
        assert_eq!(buf, [30, 40]);
    }

    #[test]
    fn read_at_generic_validate() {
        let mut data = vec![0u8; CAB_HEADER_SIZE];
        data[0..4].copy_from_slice(&CAB_SIGNATURE.to_le_bytes());
        assert!(CabArchive::validate(&*data));
    }

    // -----------------------------------------------------------------------
    // MsDosDate
    // -----------------------------------------------------------------------

    #[test]
    fn msdos_date_known_value() {
        // 2023-06-15: year offset = 43, month = 6, day = 15
        // packed = (43 << 9) | (6 << 5) | 15 = 0x56CF
        let d = MsDosDate::from(0x56CF);
        assert_eq!(d.year(), 2023);
        assert_eq!(d.month(), 6);
        assert_eq!(d.day(), 15);
    }

    #[test]
    fn msdos_date_epoch() {
        // 1980-01-01: the earliest representable date
        let d = MsDosDate::from((0 << 9) | (1 << 5) | 1);
        assert_eq!(d.year(), 1980);
        assert_eq!(d.month(), 1);
        assert_eq!(d.day(), 1);
    }

    #[test]
    fn msdos_date_max() {
        // 2107-12-31: the latest representable date
        let d = MsDosDate::from((127 << 9) | (12 << 5) | 31);
        assert_eq!(d.year(), 2107);
        assert_eq!(d.month(), 12);
        assert_eq!(d.day(), 31);
    }

    #[test]
    fn msdos_date_day_uses_five_bits() {
        // day = 31 (0x1f) needs all five bits
        let d = MsDosDate::from(31);
        assert_eq!(d.day(), 31);
    }

    #[test]
    fn msdos_date_roundtrip() {
        let raw: u16 = 0x56CF;
        let d = MsDosDate::from(raw);
        let back: u16 = d.into();
        assert_eq!(raw, back);
    }

    #[test]
    fn msdos_date_zero() {
        let d = MsDosDate::from(0u16);
        assert_eq!(d.year(), 1980);
        assert_eq!(d.month(), 0);
        assert_eq!(d.day(), 0);
    }

    // -----------------------------------------------------------------------
    // MsDosTime
    // -----------------------------------------------------------------------

    #[test]
    fn msdos_time_known_value() {
        // 14:35:42  ->  hour=14, minute=35, second/2=21
        // packed = (14 << 11) | (35 << 5) | 21 = 28672 + 1120 + 21 = 0x7475
        let t = MsDosTime::from(0x7475);
        assert_eq!(t.hour(), 14);
        assert_eq!(t.minute(), 35);
        assert_eq!(t.second(), 42);
    }

    #[test]
    fn msdos_time_midnight() {
        let t = MsDosTime::from(0u16);
        assert_eq!(t.hour(), 0);
        assert_eq!(t.minute(), 0);
        assert_eq!(t.second(), 0);
    }

    #[test]
    fn msdos_time_max() {
        // 23:59:58 (max representable)
        let t = MsDosTime::from((23 << 11) | (59 << 5) | 29);
        assert_eq!(t.hour(), 23);
        assert_eq!(t.minute(), 59);
        assert_eq!(t.second(), 58);
    }

    #[test]
    fn msdos_time_minute_uses_six_bits() {
        // minute = 59 (0x3B) needs six bits
        let t = MsDosTime::from(59u16 << 5);
        assert_eq!(t.minute(), 59);
    }

    #[test]
    fn msdos_time_second_uses_five_bits() {
        // second/2 = 29 (0x1D) needs five bits -> second = 58
        let t = MsDosTime::from(29u16);
        assert_eq!(t.second(), 58);
    }

    #[test]
    fn msdos_time_packed() {
        let raw: u16 = 0x7475;
        let t = MsDosTime::from(raw);
        assert_eq!(t.packed(), raw);
    }

    #[test]
    fn msdos_time_roundtrip() {
        let raw: u16 = 0x7475;
        let t = MsDosTime::from(raw);
        let back: u16 = t.into();
        assert_eq!(raw, back);
    }

    // -----------------------------------------------------------------------
    // MsDosFileAttr
    // -----------------------------------------------------------------------

    #[test]
    fn msdos_fileattr_none() {
        let a = MsDosFileAttr::empty();
        assert!(!a.contains(MsDosFileAttr::RDONLY));
        assert!(!a.contains(MsDosFileAttr::HIDDEN));
        assert!(!a.contains(MsDosFileAttr::SYSTEM));
        assert!(!a.contains(MsDosFileAttr::ARCH));
        assert!(!a.contains(MsDosFileAttr::EXEC));
        assert!(!a.contains(MsDosFileAttr::NAME_IS_UTF));
    }

    #[test]
    fn msdos_fileattr_rdonly() {
        let a = MsDosFileAttr::RDONLY;
        assert!(a.contains(MsDosFileAttr::RDONLY));
        assert!(!a.contains(MsDosFileAttr::HIDDEN));
    }

    #[test]
    fn msdos_fileattr_hidden() {
        let a = MsDosFileAttr::HIDDEN;
        assert!(a.contains(MsDosFileAttr::HIDDEN));
        assert!(!a.contains(MsDosFileAttr::RDONLY));
    }

    #[test]
    fn msdos_fileattr_system() {
        assert!(MsDosFileAttr::SYSTEM.contains(MsDosFileAttr::SYSTEM));
    }

    #[test]
    fn msdos_fileattr_archive() {
        assert!(MsDosFileAttr::ARCH.contains(MsDosFileAttr::ARCH));
    }

    #[test]
    fn msdos_fileattr_exec() {
        assert!(MsDosFileAttr::EXEC.contains(MsDosFileAttr::EXEC));
    }

    #[test]
    fn msdos_fileattr_utf8() {
        assert!(MsDosFileAttr::NAME_IS_UTF.contains(MsDosFileAttr::NAME_IS_UTF));
    }

    #[test]
    fn msdos_fileattr_combined() {
        // rdonly + archive + utf8 = 0x01 | 0x20 | 0x80 = 0xA1
        let a = MsDosFileAttr::RDONLY | MsDosFileAttr::ARCH | MsDosFileAttr::NAME_IS_UTF;
        assert!(a.contains(MsDosFileAttr::RDONLY));
        assert!(!a.contains(MsDosFileAttr::HIDDEN));
        assert!(!a.contains(MsDosFileAttr::SYSTEM));
        assert!(a.contains(MsDosFileAttr::ARCH));
        assert!(!a.contains(MsDosFileAttr::EXEC));
        assert!(a.contains(MsDosFileAttr::NAME_IS_UTF));
    }

    #[test]
    fn msdos_fileattr_roundtrip() {
        let raw: u16 = 0xA1;
        let a = MsDosFileAttr::from_bits_truncate(raw);
        assert_eq!(a.bits(), raw);
    }
}
