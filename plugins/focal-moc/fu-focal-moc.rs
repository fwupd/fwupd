// Copyright 2024 FocalTech Systems Co., Ltd.
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuFocalMocCmd {
    GetFwVersion = 0x30,
    GetFpVersion = 0x31,
    SetBootMode = 0x32,
    SendFirmware = 0x33,
    WakeUp = 0x34,
    TransportKeyExchange = 0xB0,
    TransportKeyConfirm = 0xB1,
}

#[repr(u8)]
enum FuFocalMocStatus {
    Ok = 0x04,
    InvalidParameter = 0x05,
    Timeout = 0x06,
    NoMemory = 0x07,
    CheckError = 0x08,
    InvalidCommand = 0x09,
    ExecutionFailure = 0x0A,
    CommandMasterKeyInvalid = 0x20,
    MasterKeyInvalid = 0x21,
    FirmwareSequence = 0x40,
    FirmwareFlash = 0x41,
    FirmwareNoSession = 0x42,
    FirmwareVerify = 0x43,
    FirmwareRecover = 0x44,
}

#[derive(ToString)]
enum FuFocalMocIapStatusLayout {
    Unknown,
    Legacy,
    Aligned,
}

#[repr(u32le)]
enum FuFocalMocFirmwareKind {
    App = 0x02,
}

#[repr(u8)]
enum FuFocalMocBootMode {
    App = 0x00,
    Iap = 0x01,
}

#[derive(ToString)]
#[repr(u8)]
enum FuFocalMocFrame {
    Soh = 0x01,
    Stx = 0x02,
    Eot = 0x04,
}

#[derive(New, Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalMocPacketHeader {
    magic: u8 == 0x02,
    length: u16be,
    command: FuFocalMocCmd,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalMocWakeUp {
    magic: u16be == 0x55AA,
}

#[derive(New, Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalMocCipherHeader {
    magic: u8 == 0x03,
    length: u16be,
    sequence: u32be,
}

#[derive(New, Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalMocFragmentHeader {
    more: u8,
    message_id: u8,
    index: u8,
    total: u8,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalMocSohV1 {
    kind: FuFocalMocFrame == Soh,
    sequence: u8,
    filename: [char; 64],
    firmware_size: u32be,
    crc32: u32be,
    _reserved: [u8; 56],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalMocDataV1 {
    kind: FuFocalMocFrame,
    sequence: u8,
    data: [u8; 1024],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalMocSohV2 {
    kind: FuFocalMocFrame == Soh,
    sequence: u16be,
    filename: [char; 64],
    firmware_size: u32be,
    crc32: u32be,
    frame_size: u16be == 1024,
    _reserved: [u8; 54],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalMocDataV2 {
    kind: FuFocalMocFrame,
    sequence: u16be,
    data: [u8; 1024],
}

#[derive(New, ParseStream, ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructFocalMocFirmwareHeader {
    magic: u32le == 0x46574844,
    kind: u32le,
    version: u32le,
    size: u32le,
    entry_offset: u32le,
    _reserved1: [u8; 28],
    digest: [u8; 32],
    signature: [u8; 64],
    _reserved2: [u8; 112],
}
