// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Parse)]
struct CcgxMetadataHdr {
    fw_checksum: u8,
    fw_entry: u32le,
    last_boot_row: u16le,   // last flash row of bootloader or previous firmware
    _reserved1: [u8; 2],
    fw_size: u32le,
    _reserved2: [u8; 9],
    metadata_valid: u16le: default=0x4359, // "CY"
    _reserved3: [u8; 4],
    boot_seq: u32le,
}

#[derive(ToString, FromString)]
enum CcgxImageType {
    Unknown,
    Single,
    DualSymmetric,          // A/B runtime
    DualAsymmetric,         // A=bootloader (fixed) B=runtime
    DualAsymmetricVariable, // A=bootloader (variable) B=runtime
    DmcComposite,           // composite firmware image for dmc
}
#[derive(ToString)]
enum CcgxFwMode {
    Boot,
    Fw1,
    Fw2,
    Last,
}
#[derive(ToString)]
enum CcgxPdResp {
    // responses
    NoResponse,
    Success = 0x02,
    FlashDataAvailable,
    InvalidCommand = 0x05,
    CollisionDetected,
    FlashUpdateFailed,
    InvalidFw,
    InvalidArguments,
    NotSupported,
    TransactionFailed = 0x0C,
    PdCommandFailed,
    Undefined,
    RaDetect = 0x10,
    RaRemoved,

    // device specific events
    ResetComplete = 0x80,
    MessageQueueOverflow,

    // type-c specific events
    OverCurrentDetected,
    OverVoltageDetected,
    TypeCConnected,
    TypeCDisconnected,

    // pd specific events and asynchronous messages
    PdContractEstablished,
    DrSwap,
    PrSwap,
    VconSwap,
    PsRdy,
    Gotomin,
    AcceptMessage,
    RejectMessage,
    WaitMessage,
    HardReset,
    VdmReceived,
    SrcCapRcvd,
    SinkCapRcvd,
    DpAlternateMode,
    DpDeviceNonnected,
    DpDeviceNotConnected,
    DpSidNotFound,
    MultipleSvidDiscovered,
    DpFunctionNotSupported,
    DpPortConfigNotSupported,

    // not a response?
    HardResetSent,
    SoftResetSent,
    CableResetSent,
    SourceDisabledStateEntered,
    SenderResponseTimerTimeout,
    NoVdmResponseReceived,
}
