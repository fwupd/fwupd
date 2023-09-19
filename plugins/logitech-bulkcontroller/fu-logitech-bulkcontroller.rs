// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum LogitechBulkcontrollerDeviceState {
    Unknown = -1,
    Offline,
    Online,
    Idle,
    InUse,
    AudioOnly,
    Enumerating,
}

#[derive(ToString)]
enum LogitechBulkcontrollerUpdateState {
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
enum LogitechBulkcontrollerCmd {
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
struct LogitechBulkcontrollerSendSyncReq {
    cmd: LogitechBulkcontrollerCmd,
    payload_length: u32le,
    sequence_id: u32le,
}
#[derive(Parse)]
struct LogitechBulkcontrollerSendSyncRes {
    cmd: LogitechBulkcontrollerCmd,
    payload_length: u32le,
    sequence_id: u32le,
}

#[derive(New)]
struct LogitechBulkcontrollerUpdateReq {
    cmd: LogitechBulkcontrollerCmd,
    payload_length: u32le,
}

#[derive(Getters)]
struct LogitechBulkcontrollerUpdateRes {
    cmd: LogitechBulkcontrollerCmd,
    _payload_length: u32le,
    cmd_req: LogitechBulkcontrollerCmd,
}

enum LogitechBulkcontrollerChecksumType {
    Sha256,
    Sha512,
    Md5,
}
