// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSteelseriesFizzConnection {
    Unexpected,
    Pairing,
    Disconnected,
    Connected,
}

#[derive(ToString)]
#[repr(u8)]
enum FuSteelseriesFizzConnectionStatus {
    NotConnected    = 0x00,
    Connected       = 0x01,
}

#[repr(u8)]
enum FuSteelseriesFizzResetMode {
    Normal          = 0x00,
    Bootloader      = 0x01,
}

#[repr(u8)]
enum FuSteelseriesFizzFilesystem {
    Receiver        = 0x01,
    Mouse           = 0x02,
}

#[repr(u8)]
enum FuSteelseriesFizzReceiverFilesystemId {
    Unknown         = 0x01,
    MainBoot        = 0x01,
    FsdataFile      = 0x02,
    FactorySettings = 0x03,
    MainApp         = 0x04,
    BackupApp       = 0x05,
    ProfilesMouse   = 0x06,
    ProfilesLighting = 0x0F,
    ProfilesDevice  = 0x10,
    ProfilesReserved = 0x11,
    Recovery        = 0x0D,
    FreeSpace       = 0xF1,
}

#[repr(u8)]
enum FuSteelseriesFizzMouseFilesystemId {
    SoftDevice      = 0x00,
    ProfilesMouse   = 0x06,
    MainApp         = 0x07,
    BackupApp       = 0x08,
    MsbData         = 0x09,
    FactorySettings  = 0x0A,
    FsdataFile      = 0x0B,
    MainBoot        = 0x0C,
    Recovery        = 0x0E,
    ProfilesLighting = 0x0F,
    ProfilesDevice   = 0x10,
    FdsPages        = 0x12,
    ProfilesBluetooth = 0x13,
    FreeSpace       = 0xF0,
}

#[repr(u8)]
enum FuSteelseriesFizzCommandError {
    Success,
    FileNotFound,
    FileTooShort,
    FlashFailed,
    PermissionDenied,
    OperationNoSupported,
}

#[derive(New, Default)]
struct FuStructSteelseriesFizzHidGetVersionReq {
    report_id: u8 == 0x04,  // GetReport
    command: u8 == 0x90,    // GetVersion
    mode: u8 == 0x00,       // string
}

#[derive(Parse, Default)]
struct FuStructSteelseriesFizzHidResponse {
    report_id: u8 = 0x04,  // GetReport
    _data: [u8; 64],
}

#[derive(ToString)]
#[repr(u8)]
enum FuSteelseriesFizzCmd {
    Reset               = 0x01,
    EraseFile           = 0x02,
    WriteAccessFile     = 0x03,
    Version2            = 0x10,
    Serial2             = 0x12,
    ReadAccessFile      = 0x83,
    FileCrc32           = 0x84,
    Version             = 0x90,
    BatteryLevel        = 0x92,
    PairedStatus        = 0xBB,
    ConnectionStatus2   = 0xB0,
    ConnectionStatus    = 0xBC,
}

#[derive(New, Default)]
struct FuStructSteelseriesFizzWriteAccessFileReq {
    cmd: FuSteelseriesFizzCmd = WriteAccessFile,
    filesystem: u8,
    id: u8,
    size: u16le,
    offset: u32le,
    data: [u8; 52],
}

#[derive(New, Default)]
struct FuStructSteelseriesFizzReadAccessFileReq {
    cmd: FuSteelseriesFizzCmd = ReadAccessFile,
    filesystem: u8,
    id: u8,
    size: u16le,
    offset: u32le,
}

#[derive(Parse)]
struct FuStructSteelseriesFizzReadAccessFileRes {
    reserved: [u8; 2],
    data: [u8; 52],
}

#[derive(New, Default)]
struct FuStructSteelseriesFizzEraseFileReq {
    cmd: FuSteelseriesFizzCmd = EraseFile,
    filesystem: u8,
    id: u8,
}

#[derive(New, Default)]
struct FuStructSteelseriesFizzResetReq {
    cmd: FuSteelseriesFizzCmd = Reset,
    mode: FuSteelseriesFizzResetMode,
}

#[derive(New, Default)]
struct FuStructSteelseriesFizzFileCrc32Req {
    cmd: FuSteelseriesFizzCmd = FileCrc32,
    filesystem: u8,
    id: u8,
}

#[derive(Parse)]
struct FuStructSteelseriesFizzFileCrc32Res {
    reserved: [u8; 2],
    calculated: u32le,
    stored: u32le,
}

#[derive(Parse)]
struct FuStructSteelseriesFizzGenericRes {
    cmd: FuSteelseriesFizzCmd,
    error: u8,
}

// gen1 only
#[derive(New, Default)]
struct FuStructSteelseriesFizzVersionReq {
    cmd: FuSteelseriesFizzCmd = Version,
    mode: u8 == 0, // string
}

// gen1 only
#[derive(New, Default)]
struct FuStructSteelseriesPairedStatusReq {
    cmd: FuSteelseriesFizzCmd == PairedStatus,
}

#[derive(Parse)]
struct FuStructSteelseriesPairedStatusRes {
    status: u8,
}

// gen1 only
#[derive(New, Default)]
struct FuStructSteelseriesConnectionStatusReq {
    cmd: FuSteelseriesFizzCmd == ConnectionStatus,
}

#[derive(Parse)]
struct FuStructSteelseriesConnectionStatusRes {
    reserved: u8,
    status: u8,
}

// gen1 only
#[derive(New, Default)]
struct FuStructSteelseriesBatteryLevelReq {
    cmd: FuSteelseriesFizzCmd = BatteryLevel,
}

#[derive(Parse)]
struct FuStructSteelseriesBatteryLevelRes {
    reserved: u8,
    level: u8,
}

// gen2 only
#[derive(New, Default)]
struct FuStructSteelseriesFizzVersion2Req {
    cmd: FuSteelseriesFizzCmd == Version2,
}

#[derive(Parse)]
struct FuStructSteelseriesVersion2Res {
    reserved: u8,
    version_receiver: [char; 12],
    _version_unknown: [char; 12],
    version_device: [char; 12],
}

// gen2 only
#[derive(New, Default)]
struct FuStructSteelseriesBatteryLevel2Req {
    cmd: FuSteelseriesFizzCmd == ConnectionStatus2, // FIXME weird, confirm!
}

#[derive(Parse)]
struct FuStructSteelseriesBatteryLevel2Res {
    reserved: [u8; 3],
    level: u8,
}

// gen2 only
#[derive(New, Default)]
struct FuStructSteelseriesConnectionStatus2Req {
    cmd: FuSteelseriesFizzCmd == ConnectionStatus2,
}

#[derive(Parse)]
struct FuStructSteelseriesConnectionStatus2Res {
    reserved: u8,
    status: FuSteelseriesFizzConnection,
}

// gen2 only
#[derive(New, Default)]
struct FuStructSteelseriesSerial2Req {
    cmd: FuSteelseriesFizzCmd == Serial2,
}

#[derive(Parse)]
struct FuStructSteelseriesSerial2Res {
    reserved: u8,
    serial: [char; 0x12],
}
