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

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoAccessoryCmd {
    target_status: u8,
    data_size: u8,
    command_class: u8,
    command_id: u8,
    flag_profile: u8,
    reserved: u8,
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoHidData {
    reportid: u8,
    cmd: FuStructLenovoAccessoryCmd,
    data: [u8; 58],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoHidDfuFw {
    reportid: u8,
    cmd: FuStructLenovoAccessoryCmd,
    file_type: u8,
    offset_address: u32be,
    data: [u8; 32],
    reserved: [u8; 21],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoHidDfuExit {
    reportid: u8,
    cmd: FuStructLenovoAccessoryCmd,
    exit_code: u8,
    reserved: [u8; 57],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoHidDfuAttribute {
    reportid: u8,
    cmd: FuStructLenovoAccessoryCmd,
    major_ver: u8,
    minor_ver: u8,
    product_pid: u16be,
    processor_id: u8,
    app_max_size: u32be,
    page_size: u32be,
    reserved: [u8; 45],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoHidDfuPrepare {
    reportid: u8,
    cmd: FuStructLenovoAccessoryCmd,
    file_type: u8,
    start_address: u32be,
    end_address: u32be,
    crc32: u32be,
    reserved: [u8; 45],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoHidDevicemode {
    reportid: u8,
    cmd: FuStructLenovoAccessoryCmd,
    mode: u8,
    reserved: [u8; 57],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoHidFwVersion {
    reportid: u8,
    cmd: FuStructLenovoAccessoryCmd,
    major: u8,
    minor: u8,
    internal: u8,
    reserved: [u8; 55],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoBleData {
    cmd: FuStructLenovoAccessoryCmd,
    data: [u8; 58],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoBleDfuFw {
    cmd: FuStructLenovoAccessoryCmd,
    file_type: u8,
    offset_address: u32be,
    data: [u8; 32],
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoBleDfuExit {
    cmd: FuStructLenovoAccessoryCmd,
    exit_code: u8,
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoBleDfuAttribute {
    cmd: FuStructLenovoAccessoryCmd,
    major_ver: u8,
    minor_ver: u8,
    product_pid: u16be,
    processor_id: u8,
    app_max_size: u32be,
    page_size: u32be,
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoBleDfuPrepare {
    cmd: FuStructLenovoAccessoryCmd,
    file_type: u8,
    start_address: u32be,
    end_address: u32be,
    crc32: u32be,
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoBleDfuCrc {
    cmd: FuStructLenovoAccessoryCmd,
    crc32: u32be,
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoBleDevicemode {
    cmd: FuStructLenovoAccessoryCmd,
    mode: u8,
}

#[derive(New,Validate,Parse,Default)]
#[repr(C,packed)]
struct FuStructLenovoBleFwVersion {
    cmd: FuStructLenovoAccessoryCmd,
    major: u8,
    minor: u8,
    internal: u8,
}
