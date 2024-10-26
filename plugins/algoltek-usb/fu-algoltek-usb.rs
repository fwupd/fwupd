// Copyright 2024 Algoltek, Inc.
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuAlgoltekCmd {
    Rdr = 0x06,
    Wrr,
    Rdv,
    En,
    Wrf = 0x10,
    Isp = 0x13,
    Ers = 0x19,
    Bot = 0x1D,
    Rst = 0x20,
}

#[derive(ParseStream, ValidateStream, Default)]
struct FuStructAlgoltekProductIdentity {
    header_len: u8,
    header: u64le == 0x4B45544C4F474C41, // 'A' 'L' 'G' 'O' 'L' 'T' 'E' 'K'
    product_name_len: u8,
    product_name: [char; 16],
    version_len: u8,
    version: [char; 48],
}

#[derive(New)]
struct FuStructAlgoltekCmdAddressPkt {
    len: u8,
    cmd: u8,
    address: u16be,
    value: u16be,
    reserved: [u8; 4],
    checksum: u8,
}

#[derive(New)]
struct FuStructAlgoltekCmdTransferPkt {
    len: u8,
    cmd: u8,
    address: u16be,
    data: [u8; 61],
    checksum: u8,
}
