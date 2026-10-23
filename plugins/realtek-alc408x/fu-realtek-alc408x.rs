// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: LGPL-2.1-or-later

// vendor requests, all sent to the device recipient on the control endpoint
enum FuRealtekAlc408xRequest {
    ReadInfo = 0x07,       // value: index of the 8-byte info block
    ResetToRom = 0x22,
    WriteReg = 0x24,       // idx: register address
    ReadReg = 0x25,        // idx: register address
    ReadFlash = 0x2E,      // value: address bits 23:16 in the high byte, idx: bits 15:0
    WriteFlash = 0x2F,     // value: address bits 23:16 in the high byte, idx: bits 15:0
    ReadFlashStatus = 0x30,
    WriteFlashStatus = 0x31,
    EraseFlash = 0x35,     // erases the 64 KiB block at the address
    GetEraseStatus = 0x36,
}

// registers in the MCU address space
enum FuRealtekAlc408xReg {
    WriteProtectConfig1 = 0x050C,
    HwMode = 0x050F,
    WriteProtectConfig2 = 0x0512,
    HwModeSelect = 0x051B,
    ChipId = 0xFE21,       // u16be
}

// returned by ReadInfo, 8 bytes at a time
#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructRealtekAlc408xInfo {
    vid: u16le,
    pid: u16le,
    reserved: [u8; 2],
    chip_id: u16be,
    reserved: [u8; 8],
    build_date: [char; 8],
    signature: [char; 8] == "REALSIL",
    reserved: [u8; 34],
    magic: u32le == 0x12345678,
    chip: u16le,
    reserved: [u8; 4],
    version_hi: u32le,
    version_lo: u32le,
    reserved: [u8; 20],
}

// at offset 0x1000 of the image, big endian like the rest of the 8051 code
#[derive(New, ValidateStream, ParseStream, Parse, Default)]
#[repr(C, packed)]
struct FuStructRealtekAlc408xFwHdr {
    unknown1: u16be = 0x3114,
    magic: u32be == 0x12345678,
    chip: u16be = 0x4080,
    unknown2: u32be = 0x1,
    version_hi: u32be,
    version_lo: u32be,
}
