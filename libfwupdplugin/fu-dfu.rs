// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct DfuFtr {
    release: u16le,
    pid: u16le,
    vid: u16le,
    ver: u16le,
    sig: [char; 3]: const=UFD,
    len: u8: default=$struct_size,
    crc: u32le,
}
#[derive(New, Validate, Parse)]
struct DfuseHdr {
    sig: [char; 5]: const=DfuSe,
    ver: u8: const=0x01,
    image_size: u32le,
    targets: u8,
}
#[derive(New, Validate, Parse)]
struct DfuseImage {
    sig: [char; 6]: const=Target,
    alt_setting: u8,
    target_named: u32le,
    target_name: [char; 255],
    target_size: u32le,
    chunks: u32le,
}
#[derive(New, Validate, Parse)]
struct DfuseElement {
    address: u32le,
    size: u32le,
}
