// Copyright 2024 Algoltek, Inc.
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuAgUsbcrOffset {
    FirmwareStartAddr = 0x0B,
    FirmwareLen = 0x0D,
    EmmcVer = 0x1FE,
    AppVerFromEnd = 0x33,
    EmmcSupportVerFromBootVer = 0x2A,
}

enum FuAgUsbcrScsiopVendor {
    EepromRd = 0xC0,
    EepromWr,
    FirmwareRevision = 0xC3,
    GenericCmd = 0xC7,
}

enum FuAgUsbcr {
    Wrsr = 0x01,
    Rdsr = 0x05,
    Wren,
    Erase = 0xC7,
}

#[derive(New, Default)]
struct FuStructAgUsbcrRegCdb {
    opcode: u8 == 0xC7,
    subopcode: u8 == 0x1F,
    sig: u16be == 0x058F,
    cmd: u8,
    subcmd: u8,
    sig2: u32be == 0x30353846,
    ramdest: u8,
    addr:u16be,
    val:u8,
    reserved:[u8; 2],
}

#[derive(New, Default)]
struct FuStructAgUsbcrResetCdb {
    opcode: u8 == 0xC7,
    subopcode: u8 == 0x1F,
    sig: u16be == 0x058F,
    cmd: u8,
    subcmd: u8,
    sig2: u32be == 0x30353846,
    val: u8,
    val2:u8,
    reserved:[u8; 4],
}

#[derive(New, Default)]
struct FuStructAgUsbcrSpiCdb {
    opcode: u8 == 0xC7,
    subopcode: u8 == 0x1F,
    sig: u16be == 0x058F,
    cmd: u8,
    addr: u16be,
    bufsz: u8,
    tag: u8,
    valid: u8,
    spisig1: u8,
    spisig2: u8,
    spicmd: u8,
    reserved:[u8; 3],
}
