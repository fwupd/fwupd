// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(Parse)]
struct FuStructAudioSerialNumber {
    mac_address: [u8; 6],
    pid: u16le,
    year: u16le,
    month: u8,
    day: u8,
}

#[repr(u16le)]
enum FuUsbCmdId {
    Init = 0xCC01,
    FirmwareDownload = 0xCC03,
    ReadVersion = 0xCC07,
}

#[repr(u16le)]
enum FuUsbCmdStatus {
    Ok = 0x0, // inferred
    Req = 0xFFFF,
    InitReq = 0xBEEF,
    InitReqAck = 0x0999,
}

#[derive(New)]
struct FuStructUsbInitRequest {
    id: FuUsbCmdId == Init,
    status: FuUsbCmdStatus == InitReq,
    len: u32le == 0,
}

#[derive(Parse)]
struct FuStructUsbInitResponse {
    id: FuUsbCmdId == Init,
    status: FuUsbCmdStatus == InitReqAck,
    len: u32le == 0, // inferred
}

#[derive(New)]
struct FuStructUsbFirmwareDownloadRequest {
    id: FuUsbCmdId == FirmwareDownload,
    status: FuUsbCmdStatus == Req,
    len: u32le,
    fw_version: [char; 16],
}

#[derive(Parse)]
struct FuStructUsbFirmwareDownloadResponse {
    id: FuUsbCmdId == FirmwareDownload,
    status: FuUsbCmdStatus == Ok,
    len: u32le,
}

#[derive(New)]
struct FuStructUsbReadVersionRequest {
    id: FuUsbCmdId == ReadVersion,
    status: FuUsbCmdStatus == Req,
    len: u32le == 0,
}

#[repr(u32le)]
enum FuUsbImageState {
    New = 0x0,
    Valid = 0x1,
    Invalid = 0x2,
}

#[derive(Parse)]
struct FuStructUsbReadVersionResponse {
    fw_version: [char; 16],
    img_state: FuUsbImageState,
}

#[derive(Parse)]
struct FuStructUsbProgressResponse {
    completed: u32le,
}
