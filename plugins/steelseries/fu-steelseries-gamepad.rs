// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSteelseriesGamepadCmd {
    Erase = 0xA1,
    GetVersions = 0x12,
    BootRuntime = 0xA6,
    BootLoader = 0x02,
    WriteChunk = 0xA3,
    WriteChecksum = 0xA5,
}

#[derive(New, Default)]
struct FuStructSteelseriesGamepadEraseReq {
    cmd: FuSteelseriesGamepadCmd == Erase,
    magic1: u8 == 0xAA,
    magic2: u8 == 0x55,
    reserved: [u8; 5],
    unknown08: u8, // FIXME
    unknown09: u8, // FIXME
    reserved: [u8; 3],
    unknown13: u8, // FIXME
}

#[derive(New, Default)]
struct FuStructSteelseriesGamepadGetVersionsReq {
    cmd: FuSteelseriesGamepadCmd == GetVersions,
}

#[derive(Parse, Default)]
struct FuStructSteelseriesGamepadGetVersionsRes {
    cmd: FuSteelseriesGamepadCmd == GetVersions,
    runtime_version: u16le,
    bootloader_version: u16le,
}

#[derive(New, Default)]
struct FuStructSteelseriesGamepadBootRuntimeReq {
    cmd: FuSteelseriesGamepadCmd == BootRuntime,
}

#[derive(New, Default)]
struct FuStructSteelseriesGamepadBootLoaderReq {
    cmd: FuSteelseriesGamepadCmd == BootLoader,
    unknown: u8 == 0x08, //FIXME??
}

#[derive(New, Default)]
struct FuStructSteelseriesGamepadWriteChunkReq {
    cmd: FuSteelseriesGamepadCmd == WriteChunk,
    block_id: u16le,
    data: [u8; 32],
    checksum: u16le,
}

#[derive(New, Default)]
struct FuStructSteelseriesGamepadWriteChecksumReq {
    cmd: FuSteelseriesGamepadCmd == WriteChecksum,
    magic1: u8 == 0xAA,
    magic2: u8 == 0x55,
    checksum: u32le,
}

#[derive(Parse, Default)]
struct FuStructSteelseriesGamepadWriteChecksumRes {
    magic1: u8 == 0xAA,
    magic2: u8 == 0x55,
    unknown1: u8 == 0x55,
    unknown2: u8 == 0x01,
}
