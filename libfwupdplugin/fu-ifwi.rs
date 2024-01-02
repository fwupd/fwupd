// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, ValidateStream, ParseStream)]
struct IfwiCpd {
    header_marker: u32le == 0x44504324,
    num_of_entries: u32le,
    header_version: u8,
    entry_version: u8,
    header_length: u8 = $struct_size,
    checksum: u8,
    partition_name: u32le,
    crc32: u32le,
}
#[derive(New, ParseStream)]
struct IfwiCpdEntry {
    name: [char; 12],
    offset: u32le,
    length: u32le,
    _reserved1: [u8; 4],
}
#[derive(New, ParseStream)]
struct IfwiCpdManifest {
    header_type: u32le,
    header_length: u32le,		// dwords
    header_version: u32le,
    flags: u32le,
    vendor: u32le,
    date: u32le,
    size: u32le,				// dwords
    id: u32le,
    rsvd: u32le,
    version: u64le,
    svn: u32le,
}
#[derive(New, ParseStream)]
struct IfwiCpdManifestExt {
    extension_type: u32le,
    extension_length: u32le,
}
#[derive(New, ValidateStream, ParseStream)]
struct IfwiFpt {
    header_marker: u32le == 0x54504624,
    num_of_entries: u32le,
    header_version: u8 = 0x20,
    entry_version: u8 == 0x10,
    header_length: u8 = $struct_size,
    flags: u8,
    ticks_to_add: u16le,
    tokens_to_add: u16le,
    uma_size: u32le,
    crc32: u32le,
    fitc_major: u16le,
    fitc_minor: u16le,
    fitc_hotfix: u16le,
    fitc_build: u16le,
}
#[derive(New, ParseStream)]
struct IfwiFptEntry {
    partition_name: u32le,
    _reserved1: [u8; 4],
    offset: u32le,
    length: u32le,		// bytes
    _reserved2: [u8; 12],
    partition_type: u32le,	// 0 for code, 1 for data, 2 for GLUT
}
