// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct CfuGetVersionRsp {
    component_cnt: u8,
    _reserved: u16le,
    flags: u8,
}

#[derive(New, Validate, Parse)]
struct CfuGetVersionRspComponent {
    fw_version: u32le,
    flags: u8,
    component_id: u8,
    _vendor_specific: u16le,
}

#[repr(u8)]
enum CfuOfferInfoCode {
    StartEntireTransaction = 0x00,
    StartOfferList = 0x01,
    EndOfferList = 0x02,
}

#[derive(ToString)]
#[repr(u8)]
enum CfuRrCode {
    OfferRejectOldFirmware = 0x00,
    OfferRejectInvComponent = 0x01,
    UpdateOfferSwapPending = 0x02,
    WrongBank = 0x04,
    SignRule = 0xE0,
    VerReleaseDebug = 0xE1,
    DebugSameVersion  = 0xE2,
}

#[derive(ToString)]
#[repr(u8)]
enum CfuOfferStatus {
    Skip = 0x00,
    Accept = 0x01,
    Reject = 0x02,
    Busy = 0x03,
    Command = 0x04,
    CmdNotSupported = 0xFF,
}

#[derive(New)]
struct CfuOfferInfoReq {
    code: CfuOfferInfoCode,
    _reserved1: u8,
    component_id: u8 == 0xFF,
    token: u8 == 0xDE, // chosen by dice roll
    _reserved2: [u8; 12],
}

#[derive(Parse)]
struct CfuOfferRsp {
    _reserved1: [u8; 3],
    token: u8,
    _reserved2: [u8; 4],
    rr_code: CfuRrCode,
    _reserved3: [u8; 3],
    status: CfuOfferStatus,
    _reserved3: [u8; 3],
}

#[repr(u8)]
enum CfuContentFlag {
    LastBlock = 0x40,
    FirstBlock = 0x80,
}

#[derive(ToString)]
#[repr(u8)]
enum CfuContentStatus {
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
struct CfuContentReq {
    flags: CfuContentFlag,
    data_length: u8,
    seq_number: u16le,
    address: u32le,
}

#[derive(New, Getters)]
struct CfuContentRsp {
    seq_number: u16le,
    _reserved1: u16le,
    status: CfuContentStatus,
    _reserved2: [u8; 3],
    _reserved3: [u8; 4],
    _reserved4: [u8; 4],
}
