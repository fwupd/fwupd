// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(Parse)]
struct AudioSerialNumber {
    mac_address: [u8; 6],
    pid: u16le,
    year: u16le,
    month: u8,
    day: u8,
}

#[repr(u16le)]
enum UsbCmdId {
    Init = 0xCC01,
    FirmwareDownload = 0xCC03,
    ReadVersion = 0xCC07,
}

#[repr(u16le)]
enum UsbCmdStatus {
    Ok = 0x0, // inferred
    Req = 0xFFFF,
    InitReq = 0xBEEF,
    InitReqAck = 0x0999,
}

#[derive(New)]
struct UsbInitRequest {
    id: UsbCmdId == Init,
    status: UsbCmdStatus == InitReq,
    len: u32le == 0,
}

#[derive(Parse)]
struct UsbInitResponse {
    id: UsbCmdId == Init,
    status: UsbCmdStatus == InitReqAck,
    len: u32le == 0, // inferred
}

#[derive(New)]
struct UsbFirmwareDownloadRequest {
    id: UsbCmdId == FirmwareDownload,
    status: UsbCmdStatus == Req,
    len: u32le,
    fw_version: [char; 16],
}

#[derive(Parse)]
struct UsbFirmwareDownloadResponse {
    id: UsbCmdId == FirmwareDownload,
    status: UsbCmdStatus == Ok,
    len: u32le,
}

#[derive(New)]
struct UsbReadVersionRequest {
    id: UsbCmdId == ReadVersion,
    status: UsbCmdStatus == Req,
    len: u32le == 0,
}

#[repr(u32le)]
enum UsbImageState {
    New = 0x0,
    Valid = 0x1,
    Invalid = 0x2,
}

#[derive(Parse)]
struct UsbReadVersionResponse {
    fw_version: [char; 16],
    img_state: UsbImageState,
}

#[derive(Parse)]
struct UsbProgressResponse {
    completed: u32le,
}
