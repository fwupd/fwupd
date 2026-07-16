/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//!
//! Binary wire structures to read and write CAB files.
//!
//! This is the raw binary format representation, naming
//! of struct and fields reflects the
//! [Microsoft Cabinet Format](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85))
//! documentation.
//!
//! Raw `CFFoo` structs are always of fixed size but may be
//! followed by optional and/or variable-length entities. The caller is
//! responsible for correct reading of this optional and/or variable-length
//! data.

use std::io::Write;
use zerocopy::byteorder::little_endian::{U16 as U16LE, U32 as U32LE};
use zerocopy::{FromBytes, Immutable, IntoBytes, KnownLayout};

use crate::declare_bitflags;
use crate::streams::ReadSeek;

use super::{CabError, ReadBytesAtExt};
use super::{MsDosDate, MsDosTime};

/// Cabinet header magic: "MSCF" as little-endian u32.
pub const CF_SIGNATURE: u32 = 0x46_43_53_4D; // 'M','S','C','F'
/// Supported major version
pub const CF_MAJOR_VERSION: u8 = 1;
/// Supported minor version
pub const CF_MINOR_VERSION: u8 = 3;

const CFHEADER_SIZE: usize = 36;
const CFFOLDER_SIZE: usize = 8;
const CFFILE_SIZE: usize = 16;
const CFDATA_SIZE: usize = 8;
const CFHEADER_RESERVE_SIZE: usize = 4;

/// Marker trait for fixed-size, `repr(C, packed)` structs that can be read
/// directly from a byte source.
///
/// # Safety
/// Only implement for types that are `#[repr(C, packed)]` with no implicit
/// padding bytes, so their in-memory representation is the correct on-disk
/// wire format.
pub trait FixedSizeIO {
    /// Returns the size of this struct in bytes
    #[must_use]
    fn size_in_bytes() -> usize
    where
        Self: Sized,
    {
        std::mem::size_of::<Self>()
    }

    /// Read `Self` from the givens source at the given offset, returning
    /// `Self` and the number of bytes read.
    ///
    /// # Errors
    ///
    /// Returns an error if the read fails.
    fn read(
        mut src: &mut (impl ReadSeek + ?Sized),
        offset: usize,
    ) -> Result<(Self, usize), CabError>
    where
        Self: Sized + FromBytes + KnownLayout,
    {
        let nbytes = Self::size_in_bytes();
        let data = src.read_bytes_at(offset, nbytes)?;
        let value = Self::read_from_bytes(&data)
            .map_err(|e| CabError::Format(format!("size mismatch reading wire struct: {e}")))?;
        Ok((value, nbytes))
    }

    /// Write `Self` to the given destination buffer.
    ///
    /// # Errors
    ///
    /// Returns an error if the destination buffer is insufficiently large.
    fn write(&self, dst: &mut impl Write) -> std::io::Result<usize>
    where
        Self: Sized + IntoBytes + Immutable,
    {
        let nbytes = Self::size_in_bytes();

        let bytes: &[u8] = IntoBytes::as_bytes(self);
        dst.write_all(bytes)?;

        Ok(nbytes)
    }
}

/// Compression type used in a cabinet folder.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
pub enum CFCompressionType {
    /// No compression.
    None = 0x0000,
    /// MSZIP (deflate) compression.
    MsZip = 0x0001,
}

impl TryFrom<U16LE> for CFCompressionType {
    type Error = CabError;

    /// Parse from the raw 16-bit value (low 4 bits only, per the CAB spec).
    fn try_from(v: U16LE) -> Result<Self, CabError> {
        match v.get() & 0x000F {
            0x0000 => Ok(CFCompressionType::None),
            0x0001 => Ok(CFCompressionType::MsZip),
            other => Err(CabError::NotSupported(format!(
                "unsupported compression type 0x{other:04x}"
            ))),
        }
    }
}

impl From<CFCompressionType> for U16LE {
    fn from(ct: CFCompressionType) -> U16LE {
        U16LE::new(ct as u16)
    }
}

impl From<CFCompressionType> for u16 {
    fn from(ct: CFCompressionType) -> u16 {
        ct as u16
    }
}

// Note: zerocopy's U16's BitOr implementation isn't const so we can't make
// the backing type for this U16LE. Means we have to use u16 in the struct
// and convert accordingly.
declare_bitflags! {
    /// Cabinet header flags.
    ///
    /// ```c
    /// #define cfhdrPREV_CABINET       0x0001
    /// #define cfhdrNEXT_CABINET       0x0002
    /// #define cfhdrRESERVE_PRESENT    0x0004
    /// ```
    /// See [Microsoft Cabinet Format](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85)#cfheader)
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct CFHeaderFlags / CFHeaderFlag : u16 {
        /// Archive has a previous cabinet.
        HasPrev = 0x0001;
        /// Archive has a next cabinet.
        HasNext = 0x0002;
        /// Archive has reserved fields.
        HasReserve = 0x0004;
    }
}

/// MS Cabinet file header. This header comprises the first 36 bytes on disk.
///
/// ```c
/// struct CFHEADER
/// {
///   u1  signature[4]      /* file signature */
///   u4  reserved1         /* reserved */
///   u4  cbCabinet         /* size of this cabinet file in bytes */
///   u4  reserved2         /* reserved */
///   u4  coffFiles         /* offset of the first CFFILE entry */
///   u4  reserved3         /* reserved */
///   u1  versionMinor      /* cabinet file format version, minor */
///   u1  versionMajor      /* cabinet file format version, major */
///   u2  cFolders          /* number of CFFOLDER entries in this cabinet */
///   u2  cFiles            /* number of CFFILE entries in this cabinet */
///   u2  flags             /* cabinet file option indicators */
///   u2  setID             /* must be the same for all cabinets in a set */
///   u2  iCabinet;         /* number of this cabinet file in a set */
///   u2  cbCFHeader;       /* (optional) size of per-cabinet reserved area */
///   u1  cbCFFolder;       /* (optional) size of per-folder reserved area */
///   u1  cbCFData;         /* (optional) size of per-datablock reserved area */
///   u1  abReserve[];      /* (optional) per-cabinet reserved area */
///   u1  szCabinetPrev[];  /* (optional) name of previous cabinet file */
///   u1  szDiskPrev[];     /* (optional) name of previous disk */
///   u1  szCabinetNext[];  /* (optional) name of next cabinet file */
///   u1  szDiskNext[];     /* (optional) name of next disk */
/// };
/// ```
/// See [Microsoft Cabinet Format](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85)#cfheader)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, FromBytes, IntoBytes, KnownLayout, Immutable)]
#[allow(non_snake_case)]
pub struct CFHeader {
    pub signature: U32LE, // 0x00 "MSCF"
    pub reserved1: U32LE, // 0x04
    pub cbCabinet: U32LE, // 0x08 size in bytes
    pub reserved2: U32LE, // 0x0C
    pub coffFiles: U32LE, // 0x10 offset of first cffile
    pub reserved3: U32LE, // 0x14
    pub versionMinor: u8, // 0x18
    pub versionMajor: u8, // 0x19
    pub cFolders: U16LE,  // 0x1A number of folder entries
    pub cFiles: U16LE,    // 0x1C number of file entries
    pub flags: U16LE,     // 0x1E
    pub setID: U16LE,     // 0x20 id for this set of cab files
    pub iCabinet: U16LE,  // 0x22 index of this cab file within the set

                          // Optionally followed by a CFHeaderReserve of size CFHeaderReserve::cbCFHeader size
                          // Optionally followed by the name of the previous cab file
                          // Optionally followed by the name of the previous disk
                          // Optionally followed by the name of the next cab file
                          // Optionally followed by the name of the next disk
}

const _: () = assert!(std::mem::size_of::<CFHeader>() == CFHEADER_SIZE);

impl FixedSizeIO for CFHeader {}

/// Optional reserved-size header immediately following the [`CFHeader`]. Only
/// present when `CFHeaderFlag::HasReserve` is set.
///
/// If `sz_header` is nonzero, this struct is immediately followed by
/// `sz_header` bytes of reserved space.
///
/// See [Microsoft Cabinet Format](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85)#cfheader)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, FromBytes, IntoBytes, KnownLayout)]
#[allow(non_snake_case, clippy::struct_field_names)]
pub struct CFHeaderReserve {
    pub cbCFHeader: U16LE, // bytes of per-cabinet reserved data
    pub cbCFFolder: u8,    // bytes of per-folder reserved data
    pub cbCFData: u8,      // bytes of per-data-block reserved data
                           // Optionally followed by CFHeaderReserve::sz_header bytes
}

const _: () = assert!(std::mem::size_of::<CFHeaderReserve>() == CFHEADER_RESERVE_SIZE);

impl FixedSizeIO for CFHeaderReserve {}

/// A folder entry within a cab file.
///
/// ```c
/// struct CFFOLDER
/// {
///   u4  coffCabStart;  /* offset of the first CFDATA block in this folder */
///   u2  cCFData;       /* number of CFDATA blocks in this folder */
///   u2  typeCompress;  /* compression type indicator */
///   u1  abReserve[];   /* (optional) per-folder reserved area */
/// };
/// See [Microsoft Cabinet Format](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85)#cffolder)
///
/// This struct is immediately followed by `CFHeaderReserve::sz_folder` bytes.
///
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, FromBytes, IntoBytes, KnownLayout, Immutable)]
#[allow(non_snake_case)]
pub struct CFFolder {
    pub coffCabStart: U32LE, // offset of first data block
    pub cCFData: U16LE,      // number of data blocks
    pub typeCompress: U16LE, // raw compression type value
}

const _: () = assert!(std::mem::size_of::<CFFolder>() == CFFOLDER_SIZE);

impl FixedSizeIO for CFFolder {}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CFFolderIndex {
    Index(u16),
    ContinuedFromPrev,
    ContinuedToNext,
    ContinuedFromPrevAndNext,
}

impl From<U16LE> for CFFolderIndex {
    fn from(v: U16LE) -> Self {
        match v.get() {
            0xfffd => CFFolderIndex::ContinuedFromPrev,
            0xfffe => CFFolderIndex::ContinuedToNext,
            0xffff => CFFolderIndex::ContinuedFromPrevAndNext,
            idx => CFFolderIndex::Index(idx),
        }
    }
}

impl From<CFFolderIndex> for U16LE {
    fn from(idx: CFFolderIndex) -> U16LE {
        U16LE::new(u16::from(idx))
    }
}

impl From<CFFolderIndex> for u16 {
    fn from(idx: CFFolderIndex) -> u16 {
        match idx {
            CFFolderIndex::ContinuedFromPrev => 0xfffd,
            CFFolderIndex::ContinuedToNext => 0xfffe,
            CFFolderIndex::ContinuedFromPrevAndNext => 0xffff,
            CFFolderIndex::Index(i) => i,
        }
    }
}

/// A file entry within a cab file.
///
/// This is the fixed component of the struct, in the file it
/// is followed by the variable-length name of the file, see
/// [`CFFile`].
///
/// ```c
/// struct CFFILE
/// {
///   u4  cbFile;           /* uncompressed size of this file in bytes */
///   u4  uoffFolderStart;  /* uncompressed offset of this file in the folder */
///   u2  iFolder;          /* index into the CFFOLDER area */
///   u2  date;             /* date stamp for this file */
///   u2  time;             /* time stamp for this file */
///   u2  attribs;          /* attribute flags for this file */
///   u1  szName[];         /* name of this file */
/// };
/// ```
/// See [Microsoft Cabinet Format](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85)#cffile)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, FromBytes, IntoBytes, KnownLayout, Immutable)]
#[allow(non_snake_case)]
pub struct CFFile {
    pub cbFile: U32LE, // uncompressed size in bytes
    pub uoffFolderStart: U32LE,
    pub iFolder: U16LE,
    pub date: MsDosDate,
    pub time: MsDosTime,
    pub attribs: U16LE,
}

const _: () = assert!(std::mem::size_of::<CFFile>() == CFFILE_SIZE);

impl FixedSizeIO for CFFile {}

/// A data block header.
///
///```c
/// struct CFDATA
/// {
///   u4  csum;         /* checksum of this CFDATA entry */
///   u2  cbData;       /* number of compressed bytes in this block */
///   u2  cbUncomp;     /* number of uncompressed bytes in this block */
///   u1  abReserve[];  /* (optional) per-datablock reserved area */
///   u1  ab[cbData];   /* compressed data bytes */
/// };
/// ```
/// See [Microsoft Cabinet Format](https://learn.microsoft.com/en-us/previous-versions//bb267310(v=vs.85)#cfdata)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, FromBytes, IntoBytes, KnownLayout, Immutable)]
#[allow(non_snake_case)]
pub struct CFData {
    pub csum: U32LE,   // checksum of this data block
    pub cbData: U16LE, // compressed size
    pub cbUncomp: U16LE, // uncompressed size
                       // Optionally followed by CFHeaderReserve::sz_data bytes
                       // Followed by sz_compressed bytes
}

const _: () = assert!(std::mem::size_of::<CFData>() == CFDATA_SIZE);

impl FixedSizeIO for CFData {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    // -----------------------------------------------------------------------
    // Binary struct parse/write roundtrip tests
    // -----------------------------------------------------------------------

    #[test]
    fn cab_header_parse() {
        let mut buf = vec![0u8; CFHEADER_SIZE];
        buf[0..4].copy_from_slice(&CF_SIGNATURE.to_le_bytes());
        buf[8..12].copy_from_slice(&100u32.to_le_bytes()); // cbCabinet
        buf[16..20].copy_from_slice(&50u32.to_le_bytes()); // coffFiles
        buf[24] = 3; // versionMinor
        buf[25] = 1; // versionMajor
        buf[26..28].copy_from_slice(&1u16.to_le_bytes()); // cFolders
        buf[28..30].copy_from_slice(&2u16.to_le_bytes()); // cFiles

        let (hdr, _) = CFHeader::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(hdr.signature.get(), CF_SIGNATURE);
        assert_eq!(hdr.cbCabinet.get(), 100);
        assert_eq!(hdr.coffFiles.get(), 50);
        assert_eq!(hdr.versionMinor, 3);
        assert_eq!(hdr.versionMajor, 1);
        assert_eq!(hdr.cFolders.get(), 1);
        assert_eq!(hdr.cFiles.get(), 2);
    }

    #[test]
    fn cab_header_roundtrip() {
        let hdr = CFHeader {
            signature: U32LE::new(CF_SIGNATURE),
            reserved1: U32LE::new(0),
            cbCabinet: U32LE::new(12345),
            reserved2: U32LE::new(0),
            coffFiles: U32LE::new(44),
            reserved3: U32LE::new(0),
            versionMinor: 3,
            versionMajor: 1,
            cFolders: U16LE::new(2),
            cFiles: U16LE::new(5),
            flags: U16LE::new(0),
            setID: U16LE::new(42),
            iCabinet: U16LE::new(0),
        };
        let mut buf: Vec<u8> = Vec::new();
        hdr.write(&mut buf).unwrap();
        assert_eq!(buf.len(), CFHEADER_SIZE);

        let (parsed, _) = CFHeader::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(parsed.signature.get(), CF_SIGNATURE);
        assert_eq!(parsed.cbCabinet.get(), 12345);
        assert_eq!(parsed.coffFiles.get(), 44);
        assert_eq!(parsed.cFolders.get(), 2);
        assert_eq!(parsed.cFiles.get(), 5);
        assert_eq!(parsed.setID.get(), 42);
    }

    #[test]
    fn cab_header_parse_too_short() {
        let buf = [0u8; 10];
        assert!(CFHeader::read(&mut Cursor::new(&buf), 0).is_err());
    }

    #[test]
    fn cab_folder_parse() {
        let mut buf = vec![0u8; CFFOLDER_SIZE];
        buf[0..4].copy_from_slice(&0x100u32.to_le_bytes());
        buf[4..6].copy_from_slice(&5u16.to_le_bytes());
        buf[6..8].copy_from_slice(&u16::from(CFCompressionType::MsZip).to_le_bytes());

        let (folder, _) = CFFolder::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(folder.coffCabStart.get(), 0x100);
        assert_eq!(folder.cCFData.get(), 5);
        assert_eq!(
            CFCompressionType::try_from(folder.typeCompress).unwrap(),
            CFCompressionType::MsZip
        );
    }

    #[test]
    fn cab_folder_roundtrip() {
        let folder = CFFolder {
            coffCabStart: U32LE::new(0x200),
            cCFData: U16LE::new(3),
            typeCompress: U16LE::new(u16::from(CFCompressionType::None)),
        };
        let mut buf: Vec<u8> = Vec::new();
        folder.write(&mut buf).unwrap();
        assert_eq!(buf.len(), CFFOLDER_SIZE);

        let (parsed, _) = CFFolder::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(parsed.coffCabStart.get(), 0x200);
        assert_eq!(parsed.cCFData.get(), 3);
        assert_eq!(
            CFCompressionType::try_from(parsed.typeCompress).unwrap(),
            CFCompressionType::None
        );
    }

    #[test]
    fn cab_data_parse() {
        let mut buf = vec![0u8; CFDATA_SIZE];
        buf[0..4].copy_from_slice(&0xDEADBEEFu32.to_le_bytes());
        buf[4..6].copy_from_slice(&100u16.to_le_bytes());
        buf[6..8].copy_from_slice(&200u16.to_le_bytes());

        let (dh, _) = CFData::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(dh.csum.get(), 0xDEADBEEF);
        assert_eq!(dh.cbData.get(), 100);
        assert_eq!(dh.cbUncomp.get(), 200);
    }

    #[test]
    fn cab_data_roundtrip() {
        let dh = CFData {
            csum: U32LE::new(0x12345678),
            cbData: U16LE::new(64),
            cbUncomp: U16LE::new(128),
        };
        let mut buf: Vec<u8> = Vec::new();
        dh.write(&mut buf).unwrap();
        assert_eq!(buf.len(), CFDATA_SIZE);

        let (parsed, _) = CFData::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(parsed.csum.get(), 0x12345678);
        assert_eq!(parsed.cbData.get(), 64);
        assert_eq!(parsed.cbUncomp.get(), 128);
    }

    #[test]
    fn cab_file_header_parse() {
        let mut buf = vec![0u8; CFFILE_SIZE];
        buf[0..4].copy_from_slice(&1000u32.to_le_bytes()); // cbFile
        buf[4..8].copy_from_slice(&0u32.to_le_bytes()); // uoffFolderStart
        buf[8..10].copy_from_slice(&0u16.to_le_bytes()); // iFolder
        buf[10..12].copy_from_slice(&0x1234u16.to_le_bytes()); // date
        buf[12..14].copy_from_slice(&0x5678u16.to_le_bytes()); // time
        buf[14..16].copy_from_slice(&0x20u16.to_le_bytes()); // attribs

        let (fhdr, _) = CFFile::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(fhdr.cbFile.get(), 1000);
        assert_eq!(fhdr.uoffFolderStart.get(), 0);
        assert_eq!(fhdr.iFolder.get(), 0);
        assert_eq!(fhdr.attribs.get(), 0x20);
    }

    #[test]
    fn cab_file_header_roundtrip() {
        let fhdr = CFFile {
            cbFile: U32LE::new(500),
            uoffFolderStart: U32LE::new(100),
            iFolder: U16LE::new(0),
            date: 0x5921u16.into(),
            time: 0x4800u16.into(),
            attribs: U16LE::new(0x20),
        };
        let mut buf: Vec<u8> = Vec::new();
        fhdr.write(&mut buf).unwrap();

        let (parsed, _) = CFFile::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(parsed.cbFile.get(), 500);
        assert_eq!(parsed.uoffFolderStart.get(), 100);
        assert_eq!(parsed.iFolder.get(), 0);
        assert_eq!(parsed.attribs.get(), 0x20);
    }

    #[test]
    fn cab_header_reserve_parse() {
        let buf: &[u8] = &[0x10, 0x00, 0x04, 0x02];
        let (reserve, _) = CFHeaderReserve::read(&mut Cursor::new(&buf), 0).unwrap();
        assert_eq!(reserve.cbCFHeader.get(), 16);
        assert_eq!(reserve.cbCFFolder, 4);
        assert_eq!(reserve.cbCFData, 2);
    }
}
