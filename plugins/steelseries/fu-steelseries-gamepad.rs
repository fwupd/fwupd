// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSteelseriesGamepadCmd {
    Erase = 0xA1,
    GetVersions = 0x12,
    Reset = 0xA6,
    WorkMode = 0x02,
    WriteChunk = 0xA3,
    WriteChecksum = 0xA5,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSteelseriesGamepadEraseReq {
    cmd: FuSteelseriesGamepadCmd == Erase,
    magic1: u8 == 0xAA,
    magic2: u8 == 0x55,
    reserved: [u8; 5],
    length: u16le,
    reserved: [u8; 3],
    magic3: u8,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSteelseriesGamepadGetVersionsReq {
    cmd: FuSteelseriesGamepadCmd == GetVersions,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructSteelseriesGamepadGetVersionsRes {
    cmd: FuSteelseriesGamepadCmd == GetVersions,
    runtime_version: u16le,
    bootloader_version: u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSteelseriesGamepadResetReq {
    cmd: FuSteelseriesGamepadCmd == Reset,
    magic1: u8 == 0xAA,
    magic2: u8 == 0x55,
}

#[repr(u8)]
enum FuSteelseriesGamepadMode {
    ControllerMode = 0x01,
    BootloaderMode = 0x08,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSteelseriesGamepadBootloaderModeReq {
    cmd: FuSteelseriesGamepadCmd == WorkMode,
    mode: FuSteelseriesGamepadMode == BootloaderMode,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSteelseriesGamepadWriteChunkReq {
    cmd: FuSteelseriesGamepadCmd == WriteChunk,
    block_id: u16le,
    data: [u8; 32],
    checksum: u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSteelseriesGamepadWriteChecksumReq {
    cmd: FuSteelseriesGamepadCmd == WriteChecksum,
    magic1: u8 == 0xAA,
    magic2: u8 == 0x55,
    checksum: u32le,
}


#[repr(u8)]
enum FuSteelseriesGamepadChecksumStatus {
    Incorrect = 0x00,
    Correct   = 0x01,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructSteelseriesGamepadWriteChecksumRes {
    cmd: FuSteelseriesGamepadCmd == WriteChecksum,
    magic1: u8 == 0xAA,
    magic2: u8 == 0x55,
    status: FuSteelseriesGamepadChecksumStatus == Correct,
    checksum: u32le,
}
