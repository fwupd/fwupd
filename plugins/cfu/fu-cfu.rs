// Copyright 2023 Richard Hughes <richard@hughsie.com>
// Copyright 2021 Michael Cheng <michael.cheng@emc.com.tw>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, Parse)]
struct FuStructCfuGetVersionRsp {
    component_cnt: u8,
    _reserved: u16le,
    flags: u8,
}

#[derive(New, Parse)]
struct FuStructCfuGetVersionRspComponent {
    fw_version: u32le,
    flags: u8,
    component_id: u8,
    _vendor_specific: u16le,
}

#[derive(ToString)]
#[repr(u8)]
enum FuCfuOfferInfoCode {
    StartEntireTransaction = 0x00,
    StartOfferList = 0x01,
    EndOfferList = 0x02,
}

#[derive(ToString)]
#[repr(u8)]
enum FuCfuRrCode {
    OfferRejectOldFirmware = 0x00,
    OfferRejectInvComponent = 0x01,
    UpdateOfferSwapPending = 0x02,
    WrongBank = 0x04,
    SignRule = 0xE0,
    VerReleaseDebug = 0xE1,
    DebugSameVersion  = 0xE2,
    None  = 0xFF,
}

#[derive(ToString)]
#[repr(u8)]
enum FuCfuOfferStatus {
    Skip = 0x00,
    Accept = 0x01,
    Reject = 0x02,
    Busy = 0x03,
    Command = 0x04,
    CmdNotSupported = 0xFF,
}

#[derive(New, Default)]
struct FuStructCfuOfferInfoReq {
    code: FuCfuOfferInfoCode,
    _reserved1: u8,
    component_id: u8 == 0xFF,
    token: u8 == 0xDE, // chosen by dice roll
    _reserved2: [u8; 12],
}

#[derive(Parse)]
struct FuStructCfuOfferRsp {
    _reserved1: [u8; 3],
    token: u8,
    _reserved2: [u8; 4],
    rr_code: FuCfuRrCode,
    _reserved3: [u8; 3],
    status: FuCfuOfferStatus,
    _reserved3: [u8; 3],
}

#[repr(u8)]
enum FuCfuContentFlag {
    Verify = 0x08,
    TestReplaceFilesystem = 0x20,
    LastBlock = 0x40,
    FirstBlock = 0x80,
}

#[derive(ToString)]
#[repr(u8)]
enum FuCfuContentStatus {
    Success = 0x00,
    ErrorPrepare = 0x01,
    ErrorWrite = 0x02,
    ErrorComplete = 0x03,
    ErrorVerify = 0x04,
    ErrorCrc = 0x05,
    ErrorSignature = 0x06,
    ErrorVersion = 0x07,
    SwapPending = 0x08,
    ErrorInvalidAddr 0x09,
    ErrorNoOffer = 0x0A,
    ErrorInvalid = 0x0B,
}

#[derive(New, Getters)]
struct FuStructCfuContentReq {
    flags: FuCfuContentFlag,
    data_length: u8,
    seq_number: u16le,
    address: u32le,
}

#[derive(Parse)]
struct FuStructCfuContentRsp {
    seq_number: u16le,
    _reserved1: u16le,
    status: FuCfuContentStatus,
    _reserved2: [u8; 3],
    _reserved3: [u8; 4],
    _reserved4: [u8; 4],
}
