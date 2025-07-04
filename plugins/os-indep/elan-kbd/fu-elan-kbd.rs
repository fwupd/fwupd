// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructElanKbdFirmware {
    reset_vector: u32be == 0x21FAFF02,
}

// bootloader endpoints
enum FuElanKbdEp {
    Cmd     = 0x01,
    Status  = 0x82,
    DataOut = 0x03,
    DataIn  = 0x84,
}

enum FuElanKbdFirmwareIdx {
    Bootloader,
    App,
    Option,
}

#[repr(u8)]
enum FuElanKbdCmd {
    EntryIap                = 0x20,
    FinishedIap             = 0x21,
    CancelIap               = 0x23,
    SoftwareReset           = 0x24,
    BootCondition           = 0x25,
    GetVerSpec              = 0x40,
    GetVerFw                = 0x41,
    GetStatus               = 0x42,
    ExitAuthMode            = 0x43,
    GetAuthLock             = 0x44,
    SetAuthLock             = 0x45,
    Abort                   = 0x46,
    GetChecksum             = 0x47,
    WriteRom                = 0xA0,
    WriteRomFinish          = 0xA1,
    WriteOption             = 0xA2,
    WriteOptionFinish       = 0xA3,
    WriteChecksum           = 0xA4,
    WriteCustomInfo         = 0xA5,
    WriteCustomInfoFinish   = 0xA6,
    ReadRom                 = 0xE0,
    ReadRomFinish           = 0xE1,
    ReadOption              = 0xE2,
    ReadOptionFinish        = 0xE3,
    ReadDataRequest         = 0xE4,
}

#[derive(ToString)]
#[repr(u8)]
enum FuElanKbdDevStatus {
    Unknown,
    Idle,
    Iap,
    WriteRom,
    WriteOpt,
    WriteCsum,
    ReadRom,
    ReadOpt,
}

#[repr(u16be)]
enum FuElanKbdStatus {
    Unknown,
    Ready,
    Busy,
    Success,
    Fail,
    Error,
}

#[derive(ToString)]
#[repr(u8)]
enum FuElanKbdError {
    Unknown,
    UnknownCommand,
    CommandStage,
    DataStage,
    RomAddressInvalid,
    AuthorityKeyIncorrect,
    WriteRomNotFinished,
    WriteOptionNotFinished,
    LengthTooBig,
    LengthTooSmall,
    ChecksumIncorrect,
    WriteFlashAbnormal,
    OverRomArea,
    RomPageInvalid,
    FlashKeyInvalid,
    OptionRomAddressInvalid,
}

#[derive(ToString)]
#[repr(u8)]
enum FuElanKbdBootCond1 {
    Unknown = 0,
    P80Entry = 1,
    NoAppEntry = 2,
    AppJumpEntry = 4,
}

enum FuElanKbdChecksumType {
    Boot,
    Main,
};

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdGetVerSpecReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == GetVerSpec,
    reserved: [u8; 6],
}
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructElanKbdGetVerSpecRes {
    value: u16be,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdGetVerFwReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == GetVerFw,
    reserved: [u8; 6],
}
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructElanKbdGetVerFwRes {
    value: u16be,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdGetStatusReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == GetStatus,
    reserved: [u8; 6],
}
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructElanKbdGetStatusRes {
    reserved: [u8; 2],
    value: FuElanKbdDevStatus,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdBootConditionReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == BootCondition,
    reserved: [u8; 6],
}
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructElanKbdBootConditionRes {
    reserved: [u8; 2],
    value: FuElanKbdBootCond1,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructElanKbdCmdStatusRes {
    value: FuElanKbdStatus,
    error: FuElanKbdError,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdAbortReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == Abort,
    reserved: [u8; 6],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdSoftwareResetReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == SoftwareReset,
    reserved: [u8; 6],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdReadRomReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == ReadRom,
    addr: u16le,
    len: u16le,
    reserved: [u8; 2],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdReadRomFinishedReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == ReadRomFinish,
    csum: u8,
    reserved: [u8; 5],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdReadOptionReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == ReadOption,
    addr: u16le == 128,
    len: u16le == 128,
    reserved: [u8; 2],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdReadOptionFinishedReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == ReadOptionFinish,
    csum: u8,
    reserved: [u8; 5],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdGetAuthLockReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == GetAuthLock,
    reserved: [u8; 6],
}
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructElanKbdGetAuthLockRes {
    key: u8,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdSetAuthLockReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == SetAuthLock,
    key: u8,
    reserved: [u8; 5],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdEntryIapReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == EntryIap,
    reserved: [u8; 4],
    key: u16le == 0x7FA9,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdFinishedIapReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == FinishedIap,
    reserved: [u8; 4],
    key: u16le == 0x7FA9,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdWriteRomReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == WriteRom,
    addr: u16le,
    len: u16le,
    key: u16le == 0x7FA9,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanKbdWriteRomFinishedReq {
    tag: u8 == 0xC1,
    cmd: FuElanKbdCmd == WriteRomFinish,
    csum: u8,
    reserved: [u8; 5],
}
