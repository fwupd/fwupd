// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToBitString, FromString)]
enum FuQcFirehoseFunctions {
    Unknown = 0,
    Program = 1 << 0,
    Configure = 1 << 1,
    Power = 1 << 2,
    Read = 1 << 3,
    Getstorageinfo = 1 << 4,
    Erase = 1 << 5,
    Nop = 1 << 6,
    Setbootablestoragedrive = 1 << 7,
    Patch = 1 << 8,
}

#[repr(u32le)]
#[derive(ToString)]
enum FuQcFirehoseSaharaCommandId {
    NoCmd,
    Hello,
    HelloResponse,
    Read,
    EndOfImage,
    Done,
    DoneResponse,
    Reset,
    ResetResponse,
    Read64 = 0x12,
}

#[repr(u32le)]
#[derive(ToString)]
enum FuQcFirehoseSaharaStatus {
    Success,
    Failed,
}

#[derive(Parse)]
struct FuQcFirehoseSaharaPkt {
    command_id: FuQcFirehoseSaharaCommandId,
    hdr_length: u32le,
}

#[derive(Default, Parse)]
struct FuQcFirehoseSaharaPktHello {
    command_id: FuQcFirehoseSaharaCommandId == Hello,
    _hdr_length: u32le,
    _version: u32le,
    _compatible: u32le,
    _max_len: u32le,
    mode: u32le,
}

#[derive(Default, New)]
struct FuQcFirehoseSaharaPktHelloResp {
    command_id: FuQcFirehoseSaharaCommandId == HelloResponse,
    hdr_length: u32le == $struct_size,
    version: u32le == 2,
    compatible: u32le == 1,
    status: FuQcFirehoseSaharaStatus == Success,
    mode: u32le,
    reserved: [u32; 6],
}

#[derive(Default, Parse)]
struct FuQcFirehoseSaharaPktRead {
    command_id: FuQcFirehoseSaharaCommandId == Read,
    hdr_length: u32le == $struct_size,
    _image: u32le,
    offset: u32le,
    length: u32le,
}

#[derive(Default, Parse)]
struct FuQcFirehoseSaharaPktRead64 {
    command_id: FuQcFirehoseSaharaCommandId == Read64,
    hdr_length: u32le == $struct_size,
    _image: u64le,
    offset: u64le,
    length: u64le,
}

#[derive(Default, Parse)]
struct FuQcFirehoseSaharaPktEndOfImage {
    command_id: FuQcFirehoseSaharaCommandId == EndOfImage,
    hdr_length: u32le == $struct_size,
    _image: u32le,
    status: FuQcFirehoseSaharaStatus,
}

#[derive(Default, New)]
struct FuQcFirehoseSaharaPktDone {
    command_id: FuQcFirehoseSaharaCommandId == Done,
    hdr_length: u32le == $struct_size,
}

#[derive(Default, Parse)]
struct FuQcFirehoseSaharaPktDoneResp {
    command_id: FuQcFirehoseSaharaCommandId == DoneResponse,
    hdr_length: u32le == $struct_size,
    status: FuQcFirehoseSaharaStatus,
}
