// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum AverHidStatus {
    Ready,
    Busy,
    Dnload,
    Waitisp,
    Isping,
    Reboot,
    Fileerr,
    Powerisp,
    Version,
    Waitusr,
    Stop,
}

#[derive(ToString)]
enum AverHidCustomIspCmd {
    Status = 0x01,
    FileStart,
    FileDnload,
    FileEnd,
    Start,
    Stop,
    Reserve,
    LogStart,
    LogUpload,
    IspReboot,
    LogEnd,
    AllFileStart = 0x11,
    AllFileDnload,
    AllFileEnd,
    AllStart,
}

#[derive(ToString, Getters, New)]
struct AverHidReqIsp {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    data: [u8; 508] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Setters, Getters, New)]
struct AverHidReqIspFileStart {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    file_name: [char; 52],
    file_size: u32le,
    free_space: u32le,
    _reserved: [u8; 448] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Setters, Getters, New)]
struct AverHidReqIspFileDnload {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    data: [u8; 508] = 0xFF,
}

#[derive(Setters, Getters, New)]
struct AverHidReqIspFileEnd {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    file_name: [char; 51],
    end_flag: u8,
    file_size: u32le,
    free_space: u32le,
    _reserved: [u8; 448] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Getters, Setters)]
struct AverHidReqIspStart {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    isp_cmd: [u8; 60],
    _reserved: [u8; 448] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Getters, New)]
struct AverHidReqDeviceVersion {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x25,
    ver: [u8; 11],
    _reserved: [u8; 498] = 0xFF,
    end: u8 == 0x00,
}

#[derive(New, Getters, Validate)]
struct AverHidResIspStatus {
    report_id_custom_command: u8 == 0x09,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    status: u8,
    status_string: [char; 58],
    progress: u8,
    _reserved: [u8; 448] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Getters, Setters)]
struct AverHidResIsp {
    report_id_custom_command: u8 == 0x09,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    _reserved: [u8; 508] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Getters, New, Validate)]
struct AverHidResDeviceVersion {
    report_id_custom_command: u8 == 0x09,
    custom_cmd_isp: u8 == 0x25,
    ver: [u8; 11],
    _reserved: [u8; 498] = 0xFF,
    end: u8 == 0x00,
}
