// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, ValidateBytes, Parse)]
struct FuStructDfuFtr {
    release: u16le,
    pid: u16le,
    vid: u16le,
    ver: u16le,
    sig: [char; 3] == "UFD",
    len: u8 = $struct_size,
    crc: u32le,
}
#[derive(New, ValidateBytes, ParseBytes)]
struct FuStructDfuseHdr {
    sig: [char; 5] == "DfuSe",
    ver: u8 == 0x01,
    image_size: u32le,
    targets: u8,
}
#[derive(New, Validate, ParseBytes)]
struct FuStructDfuseImage {
    sig: [char; 6] == "Target",
    alt_setting: u8,
    target_named: u32le,
    target_name: [char; 255],
    target_size: u32le,
    chunks: u32le,
}
#[derive(New, Validate, Parse)]
struct FuStructDfuseElement {
    address: u32le,
    size: u32le,
}
