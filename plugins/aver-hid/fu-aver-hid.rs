// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum FuAverHidStatus {
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
enum FuAverHidCustomIspCmd {
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

#[derive(ToString)]
enum FuAverSafeispCustomCmd {
    GetVersion = 0x14,
    Support = 0x29,
    EraseTemp,
    UploadPrepare,
    UploadCompareChecksum,
    UploadToCx3,
    UploadToM12mo,
    UploadToM051,
    UploadToTmpm342,
    UploadToTmpm342Boot,
    UpdateStart,
}

enum FuAverSafeispAckStatus {
    Idle = 0x00,
    Success,
    Checksum,
    ParamErr,
    PrepareFail,
    UploadFail,
    DataRead,
    DataReadFail,
    DataWrite,
    DataWriteFail,
    VerifyFail,
    CompareSame,
    CompareDiff,
    Support,
}

#[derive(Getters, New)]
struct FuStructAverHidReqIsp {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    data: [u8; 508] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Setters, Getters, New)]
struct FuStructAverHidReqIspFileStart {
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
struct FuStructAverHidReqIspFileDnload {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    data: [u8; 508] = 0xFF,
}

#[derive(Setters, Getters, New)]
struct FuStructAverHidReqIspFileEnd {
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
struct FuStructAverHidReqIspStart {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    isp_cmd: [u8; 60],
    _reserved: [u8; 448] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Getters, New)]
struct FuStructAverHidReqDeviceVersion {
    report_id_custom_command: u8 == 0x08,
    custom_cmd_isp: u8 == 0x25,
    ver: [u8; 11],
    _reserved: [u8; 498] = 0xFF,
    end: u8 == 0x00,
}

#[derive(New, Getters, Validate)]
struct FuStructAverHidResIspStatus {
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
struct FuStructAverHidResIsp {
    report_id_custom_command: u8 == 0x09,
    custom_cmd_isp: u8 == 0x10,
    custom_isp_cmd: u8,
    _reserved: [u8; 508] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Getters, New, Validate)]
struct FuStructAverHidResDeviceVersion {
    report_id_custom_command: u8 == 0x09,
    custom_cmd_isp: u8 == 0x25,
    ver: [u8; 11],
    _reserved: [u8; 498] = 0xFF,
    end: u8 == 0x00,
}

#[derive(Setters, Getters, New)]
struct FuStructAverSafeispReq {
    report_id_custom_command: u8 == 0x08,
    custom_cmd: u8,
    custom_res: u16,
    custom_parm0: u32 = 0x00,
    custom_parm1: u32 = 0x00,
    data: [u8; 1012] = 0x00,
};

#[derive(New, Getters, Validate)]
struct FuStructAverSafeispRes {
    report_id_custom_command: u8 == 0x09,
    custom_cmd: u8,
    custom_res: u16,
    custom_parm0: u32,
    custom_parm1: u32,
    data: [u8; 4] = 0x00,
};

#[derive(Getters, Validate)]
struct FuStructAverSafeispResDeviceVersion {
    report_id_custom_command: u8 == 0x09,
    custom_cmd: u8 == 0x14,
    ver: [u8; 11],
    _reserved: [u8; 3] = 0x00,
}
