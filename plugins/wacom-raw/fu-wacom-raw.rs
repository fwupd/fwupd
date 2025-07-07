// Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[repr(u8)]
enum FuWacomRawOperationMode {
    Runtime = 0x00,
    Bootloader = 0x02,
}

enum FuWacomRawDeviceCmdFlags {
    None = 0,
    PollOnWaiting = 1 << 0,
    NoErrorCheck = 1 << 1,
}

enum FuWacomRawRc {
    Ok = 0x00,
    Busy = 0x80,
    Mcutype = 0x0C,
    Pid = 0x0D,
    Checksum1 = 0x81,
    Checksum2 = 0x82,
    Timeout = 0x87,
    InProgress = 0xFF,
}

// bootloader
#[repr(u8)]
enum FuWacomRawBlReportId {
    Type = 0x02,
    Set = 0x07,
    Get = 0x08,
}

#[repr(u8)]
enum FuWacomRawBlCmd {
    EraseFlash = 0x00,
    WriteFlash = 0x01,
    VerifyFlash = 0x02,
    Attach = 0x03,
    GetBlver = 0x04,
    GetMputype = 0x05,
    CheckMode = 0x07,
    EraseDatamem = 0x0E,
    AllErase = 0x90,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructWacomRawRequest {
    report_id: FuWacomRawBlReportId,
    cmd: u8,
    echo: u8,
    addr: u32le,
    size8: u8,
    data: [u8; 128],
    data_unused: [u8; 121],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructWacomRawResponse {
    report_id: FuWacomRawBlReportId,
    cmd: u8,
    echo: u8,
    resp: u8,
    _unused: [u8; 132],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructWacomRawBlVerifyResponse {
    report_id: FuWacomRawBlReportId,
    cmd: FuWacomRawBlCmd,
    echo: u8,
    addr: u32le,
    size8: u8,
    _unknown1: [u8; 6],
    pid: u16le,
    _unknown2: [u8; 120],
}

// firmware
#[repr(u8)]
enum FuWacomRawFwReportId {
    General = 0x02,
    Status = 0x04,
}

#[repr(u8)]
enum FuWacomRawFwCmd {
    QueryMode = 0x00,
    Detach = 0x02,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructWacomRawFwStatusRequest {
    report_id: FuWacomRawFwReportId == Status,
    _reserved2: [u8; 15],
}


#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructWacomRawFwStatusResponse {
    report_id: FuWacomRawFwReportId == Status,
    _reserved1: [u8; 10],
    version_major: u16le,
    version_minor: u8,
    _reserved2: [u8; 2],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructWacomRawFwDetachRequest {
    report_id: FuWacomRawFwReportId == General,
    cmd: FuWacomRawFwCmd == Detach,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructWacomRawFwQueryModeRequest {
    report_id: FuWacomRawFwReportId == General,
    cmd: FuWacomRawFwCmd == QueryMode,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructWacomRawFwQueryModeResponse {
    report_id: FuWacomRawFwReportId == General,
    mode: FuWacomRawOperationMode,
}
