// Copyright 2023 Goodix.inc <xulinkun@goodix.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuGoodixtpIcType {
    None,
    Phoenix,
    TypeNanjing,
    Mousepad,
    Normandyl,
    Berlinb,
    Yellowstone,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructGoodixBrlbHdr {
    firmware_size: u32le,
    checksum: u32le,
    _unknown: [u8; 19],
    vid: u16be,
    subsys_num: u8,
    _unknown: [u8; 12],
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructGoodixBrlbImg {
    kind: u8,
    size: u32le,
    addr: u32le,
    _unknown: [u8; 1],
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructGoodixGtx8Hdr {
    firmware_size: u32be,
    checksum: u16be,
    _unknown: [u8; 19],
    vid: u16be,
    subsys_num: u8,
    _unknown: [u8; 4],
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructGoodixGtx8Img {
    kind: u8,
    size: u32be,
    addr: u16be,
    _unknown: [u8; 1],
}
