// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString, FromString)]
#[repr(u16le)]
enum FuZipCompression {
    None = 0,
    Shrunk = 1,
    Implode = 6,
    Tokenize = 7,
    Deflate = 8,
    Deflate64 = 9,
    Bzip2 = 12,
    Lzma = 14,
    Terse = 18,
    Zstandard = 93,
    Xz = 95,
}

#[repr(u16le)]
enum FuZipFlags {
    None = 0,
    Encrypted = 1 << 0,
    DeflateHi = 1 << 1,
    DeflateLo = 1 << 2,
    DataDescriptor = 1 << 3,
    CompressedPatched = 1 << 5,
}

#[repr(u16le)]
enum FuZipExtraHeaderId {
    None = 0,
    ExtendedTimestamp = 0x5455,
    InfoZipUnix = 0x7855,
    InfoZipUnixUidGid = 0x7875,
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuStructZipExtraHdr {
    header_id: FuZipExtraHeaderId,
    datasz: u16le,
}

#[derive(ParseStream, New, Default)]
#[repr(C, packed)]
struct FuStructZipCdfh {
    magic: [char; 4] == "PK\x01\x02",
    version_generated: u16le = 0xA,
    version_extract: u16le = 0xA,
    flags: FuZipFlags,
    compression: FuZipCompression,
    file_time: u16le,
    file_date: u16le,
    uncompressed_crc: u32le,
    compressed_size: u32le,
    uncompressed_size: u32le,
    filename_size: u16le,
    extra_size: u16le,
    comment_size: u16le,
    disk_number: u16le,
    internal_attrs: u16le,
    external_attrs: u32le,
    offset_lfh: u32le,
    // filename: [u8; filename_size]
    // extra: [u8; extra_size]
    // comment: [u8; comment_size]
}

#[derive(ParseStream, New, Default)]
#[repr(C, packed)]
struct FuStructZipEocd {
    magic: [char; 4] == "PK\x05\x06",
    disk_number: u16le,
    cd_disk: u16le,
    cd_number_disk: u16le,
    cd_number: u16le,
    cd_size: u32le,
    cd_offset: u32le,
    comment_size: u16le,
    // comment: [u8; comment_size]
}

#[derive(ParseStream, New, Default)]
#[repr(C, packed)]
struct FuStructZipLfh {
    magic: [char; 4] == "PK\x03\x04",
    version_extract: u16le = 0xA,
    flags: FuZipFlags,
    compression: FuZipCompression,
    file_time: u16le,
    file_date: u16le,
    uncompressed_crc: u32le,
    compressed_size: u32le,
    uncompressed_size: u32le,
    filename_size: u16le,
    extra_size: u16le,
    // filename: [u8; filename_size]
    // extra: [u8; extra_size]
}
