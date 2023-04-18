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
}

#[derive(ToString, Getters, New)]
struct AverHidReqIsp {
    report_id_custom_command: u8: const=0x08,
    custom_cmd_isp: u8: const=0x10,
    custom_isp_cmd: u8,
    data: [u8; 508]: padding=0xFF,
}
#[derive(New, Getters)]
struct AverHidResIspStatus {
    report_id_custom_command: u8: const=0x09,
    custom_cmd_isp: u8: const=0x10,
    custom_isp_cmd: u8,
    status: u8,
    status_string: [char; 58],
    progress: u8,
    _reserved: [u8; 448]: padding=0xFF,
}
#[derive(New, Getters, New, Setters, Validate)]
struct AverHidResIsp {
    report_id_custom_command: u8: const=0x09,
    custom_cmd_isp: u8: const=0x10,
    custom_isp_cmd: u8,
    _reserved: [u8; 508]: padding=0xFF,
}
