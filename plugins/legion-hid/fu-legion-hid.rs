// Copyright 2024 Mario Limonciello <superm1@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuLegionHidReportId {
    Input = 0x4,
    Output = 0x5,
}

#[repr(u8)]
enum FuLegionVersionDeviceId {
    Main = 0x1,
    Left = 0x3,
    Right = 0x4,
}

#[retr(u8)]
enu FuLegionHidCommand {
    DeviceVersion = 0x79,
}

#[retr(u8)]
enu FuLegionHidSubCommand {
    DeviceVersion = 0x01,
}

#[derive(Getters, New)]
struct FuStructLegionHidReqDeviceVersion {
    device: u8,
    reserved: [u8; 62],
}

#[derive(Getters, New, Validate)]
struct FuStructLegionHidResDeviceVersion {
    reserved: u32,
    ver_pro: u32,
    ver_cmd: u32,
    ver_fw: u32,
    ver_hard: u32,
    reserved: [u8; 43],
}

#[repr(u32)]
enum FuLegionHidCommand {
    IapStart = 0x50,
    IapGetMaxSize = 0x51,
    IapWriteRsp = 0x52,
    IapVerify = 0x53,
}

[derive(New, Setters, Getters)]
struct FuStructLegionIapHeader {
    report_id: u8,
    protocol_len: u8,
    protocol_id: u8,
    update_sub_id: u8,
    update_dev: u8,
    update_param: u8,
    update_cmd: u8,
}

[derive(New, Setters, Getters)]
struct FuStructLegionIapStartCmd {
    header: FuStructLegionIapHeader,
    update_cmd: u8 == 0x50,
    reserved: u8 == 0,
    crc16: u16,
    size: u32,
    update_data: [u8; 50],
}

[derive(New, Setters, Getters)]
struct FuStructLegionIapGetMaxSizeCmd {
    header: FuStructLegionIapHeader,
    update_cmd: u8 == 0x51,
    reserved: u8 == 0,
    crc16: u16,
    size: u32,
    update_data: [u8; 50],
}