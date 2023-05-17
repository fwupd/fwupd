// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, ParseBytes)]
struct FuStructCcgxMetadataHdr {
    fw_checksum: u8,
    fw_entry: u32le,
    last_boot_row: u16le,   // last flash row of bootloader or previous firmware
    _reserved1: [u8; 2],
    fw_size: u32le,
    _reserved2: [u8; 9],
    metadata_valid: u16le = 0x4359, // "CY"
    _reserved3: [u8; 4],
    boot_seq: u32le,
}

#[derive(ToString, FromString)]
enum FuCcgxImageType {
    Unknown,
    Single,
    DualSymmetric,          // A/B runtime
    DualAsymmetric,         // A=bootloader (fixed) B=runtime
    DualAsymmetricVariable, // A=bootloader (variable) B=runtime
}

#[derive(ToString)]
enum FuCcgxFwMode {
    Boot,
    Fw1,
    Fw2,
    Last,
}

#[derive(ToString)]
enum FuCcgxPdResp {
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

#[derive(Parse)]
struct CcgxPureHidFwInfo {
    report_id: u8: const=0xE0,
    _reserved_1: u8,
    signature: u16le: const=0x5943,
    operating_mode: CcgxFwMode,
    bootloader_info: u8,
    bootmode_reason: u8,
    _reserved_2: u8,
    silicon_id: u32le,
    bl_version: u32le,
    _bl_version_reserved: [u8; 4],
    image1_version: u32le,
    _image1_version_reserved: [u8; 4],
    image2_version: u32le,
    _image2_version_reserved: [u8; 4],
    image1_row: u32le,
    image2_row: u32le,
    device_uid: [u8; 6],
    _reserved_3: [u8; 10],
}

#[repr(u8)]
enum FuCcgxPureHidReportId {
    Info = 0xE0,
    Command = 0xE1,
    Write = 0xE2,
    Read = 0xE3,
    Custom = 0xE4,
}

#[derive(New)]
struct CcgxPureHidWriteHdr {
    report_id: FuCcgxPureHidReportId: const=0xE2,
    pd_resp: u8,
    addr: u16le,
    data: [u8; 128],
}
