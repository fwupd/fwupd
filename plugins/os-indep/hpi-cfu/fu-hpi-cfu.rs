/*
 * Copyright 2024 HP Development Company, L.P.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[derive(ToString)]
#[repr(u8)]
enum FuHpiCfuState {
    StartEntireTransaction = 0x00,
    StartEntireTransactionAccepted = 0x01,
    StartOfferList = 0x02,
    StartOfferListAccepted = 0x03,
    UpdateOffer = 0x04,
    UpdateOfferAccepted = 0x05,
    UpdateContent = 0x06,
    UpdateSuccess = 0x07,
    UpdateOfferRejected = 0x08,
    UpdateMoreOffers = 0x09,
    EndOfferList = 0x0A,
    EndOfferListAccepted = 0x0B,
    UpdateStop = 0x0C,
    Error = 0x0D,
    CheckUpdateContent = 0x0E,
    NotifyOnReady = 0x0F,
    WaitForReadyNotification = 0x10,
    VerifyCheckSwapPendingBySendingOfferListAgain = 0x11,
    VerifyCheckSwapPendingOfferListAccepted = 0x12,
    VerifyCheckSwapPendingSendOfferAgain = 0x13,
    VerifyCheckSwapPendingOfferAccepted = 0x14,
    VerifyCheckSwapPendingSendUpdateEndOfferList = 0x15,
    VerifyCheckSwapPendingUpdateEndOfferListAccepted = 0x16,
    UpdateVerifyError = 0x17,
}

#[derive(ToString)]
#[repr(u8)]
enum FuHpiCfuFirmwareOfferReject {
    OldFw = 0x00,
    InvComponent = 0x01,
    SwapPending = 0x02,
    Mismatch = 0x03,
    Bank = 0x04,
    Platform = 0x05,
    Milestone = 0x06,
    InvPcolRev = 0x07,
    Variant = 0x08,
}

#[derive(ToString)]
#[repr(u8)]
enum FuHpiCfuFirmwareUpdateOffer {
    Skip = 0x00,
    Accept = 0x01,
    Reject = 0x02,
    Busy = 0x03,
    CommandReady = 0x04,
    CmdNotSupported = 0xFF,
}

#[derive(ToString)]
#[repr(u8)]
enum FuHpiCfuFirmwareUpdateStatus {
    Success = 0x00,
    ErrorPrepare = 0x01,
    ErrorWrite = 0x02,
    ErrorComplete = 0x03,
    ErrorVerify = 0x04,
    ErrorCrc = 0x05,
    ErrorSignature = 0x06,
    ErrorVersion = 0x07,
    SwapPending = 0x08,
    ErrorInvalidAddr = 0x09,
    ErrorNoOffer = 0x0A,
    ErrorInvalid = 0x0B,
}


#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructHpiCfuOfferCmd {
    report_id: u8,
    segment_number: u8,
    flags: u8,
    component_id: u8,
    token: u8,
    variant: u8,
    minor_version: u16le,
    major_version: u8,
    vendor_specific: u32le,
    protocol_version: u8,
    _reserved0: u8,
    product_specific: u16le,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructHpiCfuPayloadCmd {
    report_id: u8,
    flags: u8,
    length: u8,
    seq_number: u16le,
    address: u32le,
    data: [u8; 52],
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructHpiCfuBuf {
    report_id: u8,
    command: u8,
    report_data: [u8; 15],
}
