// Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
#[repr(u8)]
// Upgrade protocol OpCode
enum FuQcOpcode {
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

#[derive(ToString)]
#[repr(u8)]
enum FuQcAction {
    Proceed = 0,
    NotProceed = 1,
}

#[derive(ToString)]
#[repr(u8)]
enum FuQcReq {
    Connect = 0x02,
    Disconnect = 0x07,
}
#[derive(New)]
struct FuStructQcConnectReq {
    req: FuQcReq == Connect,
}
#[derive(New)]
struct FuStructQcDisconnectReq {
    req: FuQcReq == Disconnect,
}

#[derive(ToString)]
#[repr(u8)]
enum FuQcStatus {
    Success = 0,         // Operation succeeded
    UnexpectedError,    // Operation failed
    AlreadyConnectedWarning,       // Already connected
    InProgress,         // Requested operation failed, an upgrade is in progress
    Busy,                // UNUSED
    InvalidPowerState,  // Invalid power management state
}
#[derive(Parse)]
struct FuStructQcUpdateStatus {
    status: FuQcStatus,
}

#[derive(New)]
struct FuStructQcVersionReq {
    opcode: FuQcOpcode == HostVersionReq,
    data_len: u16be == 0x00,
}
#[derive(Parse)]
struct FuStructQcVersion {
    status: FuQcOpcode == HostVersionCfm,
    data_len: u16be == 0x0006,
    major: u16be,
    minor: u16be,
    config: u16be,
}

#[derive(New)]
struct FuStructQcAbortReq {
    opcode: FuQcOpcode == AbortReq,
    data_len: u16be = 0x00,
}
#[derive(Parse)]
struct FuStructQcAbort {
    opcode: FuQcOpcode == AbortCfm,
    data_len: u16be = 0x00,
}

#[derive(ToString)]
#[repr(u8)]
enum FuQcResumePoint {
    Start = 0,
    PreValidate,
    PreReboot,
    PostReboot,
    PostCommit,
}
#[derive(New)]
struct FuStructQcSyncReq {
    opcode: FuQcOpcode == SyncReq,
    data_len: u16be = 0x04,
    fileId: u32be,
}
#[derive(Parse)]
struct FuStructQcSync {
    opcode: FuQcOpcode == SyncCfm,
    data_len: u16be = 0x06,
    resume_point: FuQcResumePoint,
    file_id: u32be,
    protocolVersion: u8,
}

#[derive(ToString)]
#[repr(u8)]
enum FuQcStartStatus {
    Success = 0,
    Failure = 1,
}
#[derive(New)]
struct FuStructQcStartReq {
    opcode: FuQcOpcode == StartReq,
    data_len: u16be = 0x00,
}
#[derive(Parse)]
struct FuStructQcStart {
    opcode: FuQcOpcode == StartCfm,
    data_len: u16be = 0x0003,
    status: FuQcStartStatus,
    battery_level: u16be,
}

#[derive(New)]
struct FuStructQcStartDataReq {
    opcode: FuQcOpcode == StartDataReq,
    data_len: u16be = 0x00,
    data: [u8; 250],
}

#[derive(ToString)]
#[repr(u8)]
enum FuQcMoreData {
    More = 0,
    Last = 1,
}
#[derive(Parse)]
struct FuStructQcDataReq {
    opcode: FuQcOpcode == DataBytesReq,
    data_len: u16be = 0x0008,
    fw_data_len: u32be,
    fw_data_offset: u32be,
}
#[derive(New)]
struct FuStructQcData {
    opcode: FuQcOpcode == Data,
    data_len: u16be,
    last_packet: FuQcMoreData,
    data: [u8; 249],
}

#[derive(New)]
struct FuStructQcValidationReq {
    opcode: FuQcOpcode == IsValidationDoneReq,
    data_len: u16be = 0x00,
}
#[derive(Parse)]
struct FuStructQcValidation {
    opcode: FuQcOpcode, // Could be TransferCompleteInd or IsValidationDoneCfm
    data_len: u16be,
    delay: u16be,
}

#[derive(New)]
struct FuStructQcTransferComplete {
    opcode: FuQcOpcode == TransferCompleteRes,
    data_len: u16be = 0x01,
    action: FuQcAction,
}

#[derive(New)]
struct FuStructQcProceedToCommit {
    opcode: FuQcOpcode == ProceedToCommit,
    data_len: u16be = 0x01,
    action: FuQcAction,
}
#[derive(Parse)]
struct FuStructQcCommitReq {
    opcode: FuQcOpcode == CommitReq,
    data_len: u16be = 0x00,
}

#[derive(ToString)]
#[repr(u8)]
enum FuQcCommitAction {
    Upgrade = 0,
    Rollback = 1,
}
#[derive(New)]
struct FuStructQcCommitCfm {
    opcode: FuQcOpcode == CommitCfm,
    data_len: u16be = 0x01,
    action: FuQcCommitAction,
}
#[derive(Parse)]
struct FuStructQcComplete {
    opcode: FuQcOpcode == CompleteInd,
    data_len: u16be = 0x00,
}
