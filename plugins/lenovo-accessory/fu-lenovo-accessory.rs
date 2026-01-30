// Copyright 2026 Yuchao Li <liyc44@lenovo.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuLenovoAccessoryCommandClass {
    DeviceInformation = 0x00,
    DfuClass = 0x09,
}

#[repr(u8)]
enum FuLenovoAccessoryInfoId {
   FirmwareVersion = 0x01,
   DeviceMode = 0x04,
}

#[repr(u8)]
enum FuLenovoAccessoryDfuId {
    DfuAttribute = 0x01,
    DfuPrepare = 0x02,
    DfuFile = 0x03,
    DfuCrc = 0x04,
    DfuExit = 0x05,
    DfuEntry = 0x06,
}

#[repr(u8)]
enum FuLenovoAccessoryCmdDir {
    CmdSet = 0x00,
    CmdGet = 0x01,
}

#[repr(u8)]
enum FuLenovoStatus {
    NewCommand = 0x00,
    CommandBusy = 0x01,
    CommandSuccessful = 0x02,
    CommandFailure = 0x03,
    CommandTimeOut = 0x04,
    CommandNotSupport = 0x05,
}

#[repr(u8)]
enum FuLenovoDeviceMode{
    NormalMode = 0x00,
    DriverMode = 0x01,
    DfuMode = 0x02,
}

#[repr(u8)]
enum FuLenovoDfuFileType{
    HexFile = 0x00,
    BinFile = 0x01,
}

#[repr(u8)]
enum FuLenovoDfuExitCode{
    DfuSuccess = 0x00,
    Abort = 0x01,
}

#[derive(New,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoAccessoryCmd {
    target_status: u8,
    data_size: u8,
    command_class: u8,
    command_id: u8,
    flag_profile: u8,
    reserve: u8,
}

#[derive(New)]
#[repr(C,packed)]
struct FuStructLenovoDfuFwReq {
    cmd: FuStructLenovoAccessoryCmd,
    file_type: FuLenovoDfuFileType,
    offset_address: u32be,
    data: [u8; 32],
}

#[derive(New)]
#[repr(C,packed)]
struct FuStructLenovoDfuExitReq {
    cmd: FuStructLenovoAccessoryCmd,
    exit_code: FuLenovoDfuExitCode,
}

#[derive(Parse)]
#[repr(C,packed)]
struct FuStructLenovoDfuAttributeRsp {
    major_ver: u8,
    minor_ver: u8,
    product_pid: u16be,
    processor_id: u8,
    app_max_size: u32be,
    page_size: u32be,
}

#[derive(Parse)]
#[repr(C,packed)]
struct FuStructLenovoDfuCrcRsp {
    crc32: u32be,
}

#[derive(New)]
#[repr(C,packed)]
struct FuStructLenovoDfuPrepareReq {
    cmd: FuStructLenovoAccessoryCmd,
    file_type: FuLenovoDfuFileType,
    start_address: u32be,
    end_address: u32be,
    crc32: u32be,
}

#[derive(New)]
#[repr(C,packed)]
struct FuStructLenovoDevicemodeReq {
    cmd: FuStructLenovoAccessoryCmd,
    mode: FuLenovoDeviceMode,
}

//#[derive(Parse)]
#[repr(C,packed)]
struct FuStructLenovoDevicemodeRsp {
    mode: FuLenovoDeviceMode,
}

#[derive(Parse)]
#[repr(C,packed)]
struct FuStructLenovoFwVersionRsp {
    major: u8,
    minor: u8,
    internal: u8,
}
