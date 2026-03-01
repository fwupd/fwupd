// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuGoodixMocCmd {
    UpgradeInit = 0x00,
    UpgradeData = 0x01,
    Upgrade     = 0x80,
    Ack         = 0xAA,
    Reset       = 0xB4,
    Version     = 0xD0,
}

#[repr(u8)]
enum FuGoodixMocResult {
    Success = 0x00,
    // Unknown!
}

#[repr(u8)]
enum FuGoodixMocPkgType {
    Normal = 0x80,
    Eop = 0,
}

#[derive(Parse, New)]
#[repr(C, packed)]
struct FuStructGoodixMocPkgHeader {
    cmd0: FuGoodixMocCmd,
    cmd1: u8,
    pkg_flag: FuGoodixMocPkgType,
    seq: u8,
    len: u16le,
    crc8: u8,
    rev_crc8: u8,
}

#[derive(Parse)]
struct FuStructGoodixMocPkgRsp {
    result: FuGoodixMocResult,
}

// seems unused
struct FuStructGoodixMocPkgAckRsp {
    result: FuGoodixMocResult,
    cmd: u8,
    configured: u32le, // guessed!
}

#[derive(Parse)]
struct FuStructGoodixMocPkgVersionRsp {
    result: FuGoodixMocResult,
    format: [u8; 2],
    fwtype: [u8; 8],
    fwversion: [char; 8],
    customer: [u8; 8],
    mcu: [u8; 8],
    sensor: [u8; 8],
    algversion: [u8; 8],
    interface: [u8; 8],
    protocol: [u8; 8],
    flash_version: [u8; 8],
    reserved: [u8; 62],
}
