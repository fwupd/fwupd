// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u32le)]
enum FuSaharaCommandId {
    NoCmd,
    Hello,
    HelloResponse,
    ReadData,
    EndOfImageTx,
    Done,
    DoneResp,
    Reset,
    ResetResponse,
    ReadData64 = 0x12,
}

#[repr(u32le)]
enum FuSaharaStatus {
    Success,
    Failed,
}

#[repr(u32le)]
enum FuSaharaMode {
    ImageTxPending,
    ImageTxComplete,
}

#[derive(Parse)]
struct FuStructSaharaPkt {
    hdr_command_id: FuSaharaCommandId,
    hdr_length: u32le,
}

#[derive(Default)]
struct FuStructSaharaPktHelloReq {
    hdr_command_id: FuSaharaCommandId == Hello,
    hdr_length: u32le,
    version: u32le,
    version_compatible: u32le,
    max_packet_length: u32le,
    mode: u32le,
}

#[derive(Default, Parse)]
struct FuStructSaharaPktHelloRes {
    hdr_command_id: FuSaharaCommandId == Hello,
    hdr_length: u32le,
}

#[derive(Default, New)]
struct FuStructSaharaPktHelloResponseReq {
    hdr_command_id: FuSaharaCommandId == HelloResponse,
    hdr_length: u32le == 0x30,
    version: u32le == 2,
    version_compatible: u32le == 1,
    status: FuSaharaStatus == Success,
    mode: FuSaharaMode == ImageTxPending,
    reserved: [u32; 6],
}

#[derive(Default, Parse)]
struct FuStructSaharaPktReadDataRes {
    hdr_command_id: FuSaharaCommandId == ReadData,
    hdr_length: u32le,
    image_id: u32le,
    offset: u32le,
    length: u32le,
}

#[derive(Default, Parse)]
struct FuStructSaharaPktEndOfImageTxRes {
    hdr_command_id: FuSaharaCommandId == EndOfImageTx,
    hdr_length: u32le,
    image_id: u32le,
    status: FuSaharaStatus,
}

#[derive(Default, New)]
struct FuStructSaharaPktDoneReq {
    hdr_command_id: FuSaharaCommandId == Done,
    hdr_length: u32le == 0x08,
}

#[derive(Default)]
struct FuStructSaharaPktDoneRespReq {
    hdr_command_id: FuSaharaCommandId == DoneResp,
    hdr_length: u32le,
    image_transfer_status: u32le,
}

#[derive(Default, New)]
struct FuStructSaharaPktResetReq {
    hdr_command_id: FuSaharaCommandId == Reset,
    hdr_length: u32le == 0x08,
}

#[derive(Default, Parse)]
struct FuStructSaharaPktResetRes {
    hdr_command_id: FuSaharaCommandId == ResetResponse,
    hdr_length: u32le,
}

#[derive(Default, Parse)]
struct FuStructSaharaPktReadData64Res {
    hdr_command_id: FuSaharaCommandId == ReadData64,
    hdr_length: u32le,
    image_id: u64le,
    offset: u64le,
    length: u64le,
}
