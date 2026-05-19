// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructStStm32 {
    signature: u8 == 0xDE,
    address: u16le,
}

#[derive(New, Parse)]
#[repr(C, packed)]
struct FuStructStStm32Addr {
    address: u32be,
    checksum: u8,
}

#[derive(ToString)]
#[repr(u8)]
enum FuStStm32Status {
    Nack = 0x1F,
    Busy = 0x76,
    Ack = 0x79,
}

#[derive(ToString)]
enum FuStStm32Cmd {
    Get = 0x00,
    GetVersion= 0x01,
    GetId = 0x02,
    ReadMemory = 0x11,
    Go = 0x21,
    WriteMemory = 0x31,
    WriteMemoryNs = 0x32,
    Erase = 0x43,
    EraseExtended = 0x44,
    EraseExtendedNs = 0x45,
    WriteProtect = 0x63,
    WriteProtectNs = 0x64,
    WriteUnprotect = 0x73,
    WriteUnprotectNs = 0x74,
    ReadProtect = 0x82,
    ReadProtectNs = 0x83,
    ReadUnprotect = 0x92,
    ReadUnprotectNs = 0x93,
    Crc = 0xA1,
    Init = 0x7F,
    Invalid = 0xFF,
}

#[repr(u8)]
enum FuStStmI2cProtocol {
	V1_0 = 0x10,
	V1_1 = 0x11,
	V1_2 = 0x12,
	V2 = 0x20,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructStStm32GetRsp {
    length: u8,
    protocol_ver: FuStStmI2cProtocol,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructStStm32GetIdRsp {
    length: u8 == 0x2,
    cid: u16be,
}
