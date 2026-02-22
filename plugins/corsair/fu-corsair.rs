// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u16le)]
enum FuCorsairDeviceProperty {
    Mode = 0x03,
    BatteryLevel = 0x0F,
    Version = 0x13,
    BootloaderVersion = 0x14,
    Subdevices = 0x36,
}

#[repr(u8)]
enum FuCorsairDeviceMode {
    Application = 0x01,
    Bootloader = 0x03,
}

#[repr(u8)]
enum FuCorsairDestination {
    Pc = 0x00,
    Internal = 0x01,
    Self = 0x08,
    Subdevice = 0x09,
}

#[repr(u8)]
enum FuCorsairCmd {
    Unknown = 0x00,
    SetMode = 0x01,
    GetProperty = 0x02,
    Commit = 0x05,
    WriteFirst = 0x06,
    WriteNext = 0x07,
    Init = 0x0D,
    Attach = 0x10,
    Activate = 0x16,
}

#[repr(u8)]
enum FuCorsairStatus {
    Success = 0x00,
    Unknown3 = 0x03,
    NotSupported = 0x05,
}

#[derive(Default, Parse)]
struct FuStructCorsairGenericRes {
    destination: FuCorsairDestination, // guessed
    cmd: FuCorsairCmd, // guessed
    status: FuCorsairStatus,
}

#[derive(Default, New)]
struct FuStructCorsairInitReq {
    destination: FuCorsairDestination = Self,
    cmd: FuCorsairCmd == Init,
    data1: u8 == 0x00,
    data2: u8 == 0x03,
}

#[derive(Default, New)]
struct FuStructCorsairWriteFirstReq {
    destination: FuCorsairDestination = Self,
    cmd: FuCorsairCmd == WriteFirst,
    data1: u8 == 0x00,
    size: u32le,
    // variable size payload
}

#[derive(Default, New)]
struct FuStructCorsairWriteNextReq {
    destination: FuCorsairDestination = Self,
    cmd: FuCorsairCmd == WriteNext,
    data1: u8 == 0x00,
    // variable size payload
}

#[derive(Default, New)]
struct FuStructCorsairSetModeReq {
    destination: FuCorsairDestination = Self,
    cmd: FuCorsairCmd == SetMode,
    data1: u8 == 0x03,
    data2: u8 == 0x00,
    mode: FuCorsairDeviceMode,
}

#[derive(Default, New)]
struct FuStructCorsairCommitReq {
    destination: FuCorsairDestination = Self,
    cmd: FuCorsairCmd == Commit,
    data1: u8 == 0x01,
    data2: u8 == 0x00,
}

#[derive(Default, New)]
struct FuStructCorsairActivateReq {
    destination: FuCorsairDestination = Self,
    cmd: FuCorsairCmd == Activate,
    data1: u8 == 0x00,
    data2: u8 == 0x01,
    data3: u8 == 0x03,
    data4: u8 == 0x00,
    data5: u8 == 0x01,
    data6: u8 == 0x01,
    crc: u32le,
}

#[derive(Default, New)]
struct FuStructCorsairAttachReq {
    destination: FuCorsairDestination = Self,
    cmd: FuCorsairCmd == Attach,
    data1: u8 == 0x01,
    data2: u8 == 0x00,
    data3: u8 == 0x03,
    data4: u8 == 0x00,
    data5: u8 == 0x01,
}

#[derive(Default, New)]
struct FuStructCorsairGetPropertyReq {
    destination: FuCorsairDestination = Self,
    cmd: FuCorsairCmd == GetProperty,
    property: FuCorsairDeviceProperty,
}

#[derive(Default, Parse)]
struct FuStructCorsairGetPropertyRes {
    destination: FuCorsairDestination,
    cmd: FuCorsairCmd, // guessed
    status: FuCorsairStatus == Success,
    value: u32le,
}
