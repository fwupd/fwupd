// Copyright 2024 B&R Industrial Automation GmbH
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u1)]
enum FuBnrDpBootArea {
    Low,
    High,
}

#[repr(u16le)]
enum FuBnrDpPayloadFlags {
    BootArea = 1 << 0,
    CrcError = 1 << 1,
}

#[derive(Default, Setters, Parse)]
#[repr(C, packed)]
struct FuStructBnrDpPayloadHeader {
    id: [char; 4] == "DP0R",
    version: [char; 4],
    counter: u32le,
    flags: FuBnrDpPayloadFlags,
    crc: u16be,
}

// FIXME: should be repr(u4) and AuxCommand struct should have `_reserve: u4` at front
#[derive(ToString)]
#[repr(u8)]
enum FuBnrDpModuleNumber {
    Receiver = 0x00,
    Display = 0x10,
    KeyExpansion = 0x20,
}

#[derive(ToString)]
#[repr(u8)]
enum FuBnrDpOpcodes {
    Reset = 0x08,
    FwVersion = 0x10,
    InfoFlags = 0x11,
    FlashSaveHeaderInfo = 0x6A,
    FactoryData = 0x80,
    FlashUser = 0xB0,
    FlashService = 0xF0,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructBnrDpAuxCommand {
    module_number: FuBnrDpModuleNumber,
    opcode: FuBnrDpOpcodes,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructBnrDpAuxRequest {
    data_len: u16le,
    offset: u16le,
    command: FuStructBnrDpAuxCommand,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructBnrDpAuxTxHeader {
    request: FuStructBnrDpAuxRequest,
    checksum: u8,
}

#[derive(ToString)]
#[repr(u4)]
enum FuBnrDpAuxError {
    IrqCollision,
    UnknownCommand,
    Timeout,
    BadParameter,
    DeviceBusy,
    DeviceFailure,
    DataFailure,
}

// FIXME: remove this enum, AuxStatus.error should be `error_number: u4`, u2 reserve followed by
// error and busy bits
#[repr(u8)]
enum FuBnrDpAuxStatusFlags {
    Error = 1 << 6,
    Busy = 1 << 7,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructBnrDpAuxStatus {
    _reserve: u8,
    error: u8,
}

#[repr(C, packed)]
struct FuStructBnrDpAuxResponse {
    data_len: u16le,
    _reserve: [u8; 4],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructBnrDpAuxRxHeader {
    response: FuStructBnrDpAuxResponse,
    checksum: u8,
}

#[repr(u8)]
enum FuBnrDpChecksumInit {
    Rx = 0xAB,
    Tx = 0xBA,
}

#[repr(u32le)]
enum FuBnrDpInfoFlags {
    BootArea = 1 << 0,
    CrcOk = 1 << 1,
    PmeEnable = 1 << 4,
    IctEnable = 1 << 5,
    RecEnable = 1 << 6,
}

#[derive(Default, Parse)]
#[repr(C, packed)]
struct FuStructBnrDpFactoryData {
    id: [char; 4] == "FACT",
    version_struct: u8,
    version_data: u8,
    data_len: u16le,
    header_type: u16le,
    product_num: u32le,
    compat_id: u16le,
    vendor_id: u32le,
    hw_rev: [char; 5],
    serial: [char; 12],
    identification: [char; 41],
    hw_num: [char; 3],
    parent_product_num: u32le,
    parent_compat_id: u16le,
}
