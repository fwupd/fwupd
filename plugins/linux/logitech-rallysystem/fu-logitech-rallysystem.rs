// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Parse)]
#[repr(C, packed)]
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

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructUsbInitRequest {
    id: FuUsbCmdId == Init,
    status: FuUsbCmdStatus == InitReq,
    len: u32le == 0,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructUsbInitResponse {
    id: FuUsbCmdId == Init,
    status: FuUsbCmdStatus == InitReqAck,
    len: u32le == 0, // inferred
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructUsbFirmwareDownloadRequest {
    id: FuUsbCmdId == FirmwareDownload,
    status: FuUsbCmdStatus == Req,
    len: u32le,
    fw_version: [char; 16],
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructUsbFirmwareDownloadResponse {
    id: FuUsbCmdId == FirmwareDownload,
    status: FuUsbCmdStatus == Ok,
    len: u32le,
}

#[derive(New, Default)]
#[repr(C, packed)]
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
#[repr(C, packed)]
struct FuStructUsbReadVersionResponse {
    fw_version: [char; 16],
    img_state: FuUsbImageState,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructUsbProgressResponse {
    completed: u32le,
}
