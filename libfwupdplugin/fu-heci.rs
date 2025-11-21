// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuMkhiGroupId {
    Cbm,
    Pm, // no longer used
    Pwd,
    Fwcaps,
    App, // no longer used
    Fwupdate, // for manufacturing downgrade
    FirmwareUpdate,
    Bist,
    Mdes,
    MeDbg,
    Mca, // sometimes called "FPF"
    Gen = 0xFF,
}

enum FuMkhiStatus {
    Success,
    InvalidState,
    MessageSkipped,
    SizeError = 0x05,
    UnknownPerhapsNotSupported = 0x0b, // guessed
    NotSet = 0x0F, // guessed
    NotAvailable = 0x18, // guessed
    InvalidAccess = 0x84,
    InvalidParams = 0x85,
    NotReady = 0x88,
    NotSupported = 0x89,
    InvalidAddress = 0x8C,
    InvalidCommand = 0x8D,
    Failure = 0x9E,
    InvalidResource = 0xE4,
    ResourceInUse = 0xE5,
    NoResource = 0xE6,
    GeneralError = 0xFF,
}

#[repr(u8)]
enum FuMkhiCommand {
    ReadFile = 0x02,
    ReadFileEx = 0x0A,
    ArbhSvnCommit = 0x1B,
    ArbhSvnGetInfo = 0x1C,
    // not real commands, but makes debugging easier
    ReadFileResponse = 0x82,
    ReadFileExResponse = 0x8A,
    ArbhSvnCommitResponse = 0x9B,
    ArbhSvnGetInfoResponse = 0x9C,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuMkhiReadFileRequest {
    group_id: FuMkhiGroupId == Mca,
    command: FuMkhiCommand == ReadFile,
    _rsvd: u8,
    result: u8 == 0x0,
    filename: [char; 0x40],
    offset: u32le == 0x0,
    data_size: u32le,
    flags: u8,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuMkhiReadFileResponse {
    group_id: FuMkhiGroupId == Mca,
    command: FuMkhiCommand == ReadFileResponse,
    _rsvd: u8,
    result: u8,
    data_size: u32le,
    // payload here
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuMkhiReadFileExRequest {
    group_id: FuMkhiGroupId == Mca,
    command: FuMkhiCommand == ReadFileEx,
    _rsvd: u8,
    result: u8 == 0x0,
    file_id: u32le,
    offset: u32le == 0x0,
    data_size: u32le,
    flags: u8,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuMkhiReadFileExResponse {
    group_id: FuMkhiGroupId == Mca,
    command: FuMkhiCommand == ReadFileExResponse,
    _rsvd: u8,
    result: u8,
    data_size: u32le,
    // payload here
}


#[repr(u8)]
enum FuMkhiArbhSvnInfoEntryUsageId {
    CseRbe = 3,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuMkhiArbhSvnInfoEntry {
    usage_id: FuMkhiArbhSvnInfoEntryUsageId,
    _flags: u8,
    executing: u8,
    min_allowed: u8,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuMkhiArbhSvnGetInfoRequest {
    group_id: FuMkhiGroupId == Mca,
    command: FuMkhiCommand == ArbhSvnGetInfo,
    reserved: u8,
    result: u8 == 0x0,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuMkhiArbhSvnGetInfoResponse {
    group_id: FuMkhiGroupId == Mca,
    command: FuMkhiCommand == ArbhSvnGetInfoResponse,
    _rsvd: u8,
    result: u8,
    num_entries: u32le,
    // FuMkhiArbhSvnInfoEntry entries[]
}
