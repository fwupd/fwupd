// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuLogitechBulkcontrollerDeviceState {
    Unknown = -1,
    Offline,
    Online,
    Idle,
    InUse,
    AudioOnly,
    Enumerating,
}

#[derive(ToString)]
enum FuLogitechBulkcontrollerUpdateState {
    Unknown = -1,
    Current,
    Available,
    Starting = 3,
    Downloading,
    Ready,
    Updating,
    Scheduled,
    Error,
}

#[repr(u32le)]
#[derive(ToString)]
enum FuLogitechBulkcontrollerCmd {
    CheckBuffersize = 0xCC00,
    Init = 0xCC01,
    StartTransfer = 0xCC02,
    DataTransfer = 0xCC03,
    EndTransfer = 0xCC04,
    Uninit = 0xCC05,
    BufferRead = 0xCC06,
    BufferWrite = 0xCC07,
    UninitBuffer = 0xCC08,
    Ack = 0xFF01,
    Timeout = 0xFF02,
    Nack = 0xFF03,
}

#[derive(New, ToString, Getters)]
#[repr(C, packed)]
struct FuStructLogitechBulkcontrollerSendSyncReq {
    cmd: FuLogitechBulkcontrollerCmd,
    payload_length: u32le,
    sequence_id: u32le,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLogitechBulkcontrollerSendSyncRes {
    cmd: FuLogitechBulkcontrollerCmd,
    payload_length: u32le,
    sequence_id: u32le,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructLogitechBulkcontrollerUpdateReq {
    cmd: FuLogitechBulkcontrollerCmd,
    payload_length: u32le,
}

#[derive(Getters)]
#[repr(C, packed)]
struct FuStructLogitechBulkcontrollerUpdateRes {
    cmd: FuLogitechBulkcontrollerCmd,
    _payload_length: u32le,
    cmd_req: FuLogitechBulkcontrollerCmd,
}

enum FuLogitechBulkcontrollerChecksumType {
    Sha256,
    Sha512,
    Md5,
}
