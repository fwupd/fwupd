// Copyright 2025 hya1711 <591770796@qq.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuLegionHidUpgradeStep {
    Start = 0x50,
    QuerySize,
    WriteData,
    Verify,
}

#[repr(u8)]
enum FuLegionHidResponseStatus {
    Ok = 0x00,
    Fail,
    Busy,
}

#[repr(u8)]
enum FuLegionHidDeviceId {
    Unknown,
    Rx,
    Dongle,
    GamepadL,
    GamepadR,
    GamepadL2,
    GamepadR2,
    GamepadL3,
    GamepadR3,
}

#[repr(u8)]
enum FuLegionHidCmdConstant {
    Sn = 0x01,
    UpgradeSendCmd = 0x01,
    UpgradeSendData = 0x02,
    OutputReportId = 0x05,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructLegionHidUpgradeCmd {
    report_id: FuLegionHidCmdConstant = OutputReportId,
    length: u8,
    main_id: u8 = 0x53,
    sub_id: u8 = 0x11,
    device_id: FuLegionHidDeviceId,
    param: FuLegionHidCmdConstant = UpgradeSendCmd,
    data: [u8; 58],
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructLegionHidUpgradeRsp {
    report_id: u8,
    length: u8,
    main_id: u8,
    sub_id: u8,
    id: FuLegionHidDeviceId,
    param: u8,
    data_length: u8,
    step: u8,
    reserved: u8,
    response: FuLegionHidResponseStatus == Ok,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructLegionHidUpgradeQuerySizeRsp {
    report_id: u8,
    length: u8,
    main_id: u8,
    sub_id: u8,
    id: FuLegionHidDeviceId,
    param: u8,
    data_length: u8,
    step: u8,
    reserved: u8,
    response: u8,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructLegionHidUpgradeStartParam {
    length: u8 = 0x08,
    step: FuLegionHidUpgradeStep = Start,
    flag: u8 = 0x00,
    crc16: u16be,
    size: u24be,
    sn: FuLegionHidCmdConstant = Sn,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructLegionHidUpgradePacket {
    data: [u8; 32],
    sn: FuLegionHidCmdConstant = Sn,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructLegionHidNormalCmd {
    report_id: FuLegionHidCmdConstant = OutputReportId,
    length: u8,
    main_id: u8,
    sub_id: u8,
    device_id: FuLegionHidDeviceId,
    data: [u8; 59],
}

#[derive(Setters, Default, ParseStream)]
#[repr(C, packed)]
struct FuStructLegionHidBinHeader {
    mcu_size: u32le,
    mcu_version: u32le,
    left_size: u32le,
    left_version: u32le,
    right_size: u32le,
    right_version: u32le,
}
