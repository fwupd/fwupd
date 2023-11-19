// Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
#[repr(u8)]
// Upgrade protocol OpCode
enum QcOpcode {
    StartReq = 0x01,
    StartCfm = 0x02,
    DataBytesReq = 0x03,
    Data = 0x04,
    AbortReq = 0x07,
    AbortCfm = 0x08,
    TransferCompleteInd = 0x0B,
    TransferCompleteRes = 0x0C,
    ProceedToCommit = 0x0E,
    CommitReq = 0x0F,
    CommitCfm = 0x10,
    ErrorInd = 0x11,
    CompleteInd = 0x12,
    SyncReq = 0x13,
    SyncCfm = 0x14,
    StartDataReq = 0x15,
    IsValidationDoneReq = 0x16,
    IsValidationDoneCfm = 0x17,
    HostVersionReq = 0x19,
    HostVersionCfm = 0x1A,
    ErrorRes = 0x1F,
}

#[repr(u8)]
enum QcAction {
    Proceed = 0,
    NotProceed = 1,
}

#[repr(u8)]
enum QcReq {
    Connect = 0x02,
    Disconnect = 0x07,
}
#[derive(New)]
struct QcConnectReq {
    req: QcReq == Connect,
}
#[derive(New)]
struct QcDisconnectReq {
    req: QcReq == Disconnect,
}

#[derive(ToString)]
#[repr(u8)]
enum QcStatus {
    Success = 0,         // Operation succeeded
    UnexpectedError,    // Operation failed
    AlreadyConnectedWarning,       // Already connected
    InProgress,         // Requested operation failed, an upgrade is in progress
    Busy,                // UNUSED
    InvalidPowerState,  // Invalid power management state
}
#[derive(Parse)]
struct QcUpdateStatus {
    status: QcStatus,
}

#[derive(New)]
struct QcVersionReq {
    opcode: QcOpcode == HostVersionReq,
    data_len: u16be == 0x00,
}
#[derive(Parse)]
struct QcVersion {
    status: QcOpcode == HostVersionCfm,
    data_len: u16be == 0x0006,
    major: u16be,
    minor: u16be,
    config: u16be,
}

#[derive(New)]
struct QcAbortReq {
    opcode: QcOpcode == AbortReq,
    data_len: u16be = 0x00,
}
#[derive(Parse)]
struct QcAbort {
    opcode: QcOpcode == AbortCfm,
    data_len: u16be = 0x00,
}

#[derive(ToString)]
#[repr(u8)]
enum QcResumePoint {
    Start = 0,
    PreValidate,
    PreReboot,
    PostReboot,
    PostCommit,
}
#[derive(New)]
struct QcSyncReq {
    opcode: QcOpcode == SyncReq,
    data_len: u16be = 0x04,
    fileId: u32be,
}
#[derive(Parse)]
struct QcSync {
    opcode: QcOpcode == SyncCfm,
    data_len: u16be = 0x06,
    resume_point: QcResumePoint,
    file_id: u32be,
    protocolVersion: u8,
}

#[derive(ToString)]
#[repr(u8)]
enum QcStartStatus {
    Success = 0,
    Failure = 1,
}
#[derive(New)]
struct QcStartReq {
    opcode: QcOpcode == StartReq,
    data_len: u16be = 0x00,
}
#[derive(Parse)]
struct QcStart {
    opcode: QcOpcode == StartCfm,
    data_len: u16be = 0x0003,
    status: QcStartStatus,
    battery_level: u16be,
}

#[derive(New)]
struct QcStartDataReq {
    opcode: QcOpcode == StartDataReq,
    data_len: u16be = 0x00,
    data: [u8; 250],
}

#[repr(u8)]
enum QcMoreData {
    More = 0,
    Last = 1,
}
#[derive(Parse)]
struct QcDataReq {
    opcode: QcOpcode == DataBytesReq,
    data_len: u16be = 0x0008,
    fw_data_len: u32be,
    fw_data_offset: u32be,
}
#[derive(New)]
struct QcData {
    opcode: QcOpcode == Data,
    data_len: u16be,
    last_packet: QcMoreData,
    data: [u8; 249],
}

#[derive(New)]
struct QcValidationReq {
    opcode: QcOpcode == IsValidationDoneReq,
    data_len: u16be = 0x00,
}
#[derive(Parse)]
struct QcValidation {
    opcode: QcOpcode, // Could be TransferCompleteInd or IsValidationDoneCfm
    data_len: u16be,
    delay: u16be,
}

#[derive(New)]
struct QcTransferComplete {
    opcode: QcOpcode == TransferCompleteRes,
    data_len: u16be = 0x01,
    action: QcAction,
}

#[derive(New)]
struct QcProceedToCommit {
    opcode: QcOpcode == ProceedToCommit,
    data_len: u16be = 0x01,
    action: QcAction,
}
#[derive(Parse)]
struct QcCommitReq {
    opcode: QcOpcode == CommitReq,
    data_len: u16be = 0x00,
}

#[repr(u8)]
enum QcCommitAction {
    Upgrade = 0,
    Rollback = 1,
}
#[derive(New)]
struct QcCommitCfm {
    opcode: QcOpcode == CommitCfm,
    data_len: u16be = 0x01,
    action: QcCommitAction,
}
#[derive(Parse)]
struct QcComplete {
    opcode: QcOpcode == CompleteInd,
    data_len: u16be = 0x00,
}
