// Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
// SPDX-License-Identifier: LGPL-2.1-or-later


#[repr(u8)]
enum FuQcGaiaV3Features {
    Core = 0x00,
    Dfu = 0x06,
}

// Commands: 80-CH482-1, 80-CF422-1, 80-CF378-1

#[repr(u16be)]
enum FuQcGaiaV3Cmd {
    GetApiReq = 0x0000,
    GetApiResp = 0x0100,
    GetSupportedFeaturesReq = 0x0001,
    GetSupportedFeaturesResp = 0x0101,
    GetSupportedFeaturesNextReq = 0x0002,
    GetSupportedFeaturesNextResp = 0x0102,
    GetSerialReq = 0x0003,
    GetSerialResp = 0x0103,
    GetVariantReq = 0x0004,
    GetVariantResp = 0x0104,
    RegisterNotificationCmd = 0x0007,
    RegisterNotificationAck = 0x0107,
    GetTransportInfoReq = 0x000c,
    GetTransportInfoResp = 0x010c,
    SetTransportInfoReq = 0x000d,
    SetTransportInfoResp = 0x010d,
    GetSystemInfoReq = 0x0011,
    GetSystemInfoResp = 0x0111,
    UpgradeConnectCmd = 0x0c00,
    UpgradeConnectAck = 0x0d00,
    UpgradeDisconnectCmd = 0x0c01,
    UpgradeDisconnectAck = 0x0d01,
    UpgradeControlCmd = 0x0c02,
    UpgradeControlAck = 0x0d02,
 }

#[repr(u8)]
enum FuQcGaiaCmdStatus {
    Success = 0x00,
    NotSupported = 0x01,
    InsufficientResources = 0x03,
    InvalidParameter = 0x05,
    IncorrectState = 0x06,
    InProgerss = 0x07,
}

#[derive(New)]
struct FuStructQcGaiaV3ApiReq {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetApiReq,
}
#[derive(Parse)]
struct FuStructQcGaiaV3Api {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetApiResp,
    major: u8,
    minor: u8,
}

#[repr(u8)]
enum FuQcMore {
    More = 1,
    Last = 0,
}
#[derive(New)]
struct FuStructQcGaiaV3SupportedFeaturesReq {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd,
}
#[derive(Parse)]
struct FuStructQcGaiaV3SupportedFeatures {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd,
    moreFeatures: FuQcMore,
    // variable length
}

#[derive(New)]
struct FuStructQcGaiaV3SerialReq {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetSerialReq,
}
#[derive(Parse)]
struct FuStructQcGaiaV3Serial {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetSerialResp,
    // variable string
}

#[derive(New)]
struct FuStructQcGaiaV3VariantReq {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetVariantReq,
}
#[derive(Parse)]
struct FuStructQcGaiaV3Variant {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetVariantResp,
    // variable string
}

#[derive(New)]
struct FuStructQcGaiaV3GetTransportInfoReq {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetTransportInfoReq,
    key: u8,
}
#[derive(Parse)]
struct FuStructQcGaiaV3GetTransportInfo {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetTransportInfoResp,
    key: u8,
    value: u32be,
}

#[derive(New)]
struct FuStructQcGaiaV3SetTransportInfoReq {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == SetTransportInfoReq,
    key: u8,
    value: u32be,
}
#[derive(Parse)]
struct FuStructQcGaiaV3SetTransportInfo {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == SetTransportInfoResp,
    key: u8,
    value: u32be,
}

#[derive(New)]
struct FuStructQcGaiaV3UpgradeConnectCmd {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == UpgradeConnectCmd,
}

#[derive(Parse)]
struct FuStructQcGaiaV3UpgradeConnectAck {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == UpgradeConnectAck,
}

#[derive(New)]
struct FuStructQcGaiaV3UpgradeDisconnectCmd {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == UpgradeDisconnectCmd,
}
#[derive(Parse)]
struct FuStructQcGaiaV3UpgradeDisconnectAck {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == UpgradeDisconnectAck,
}

#[derive(New)]
struct FuStructQcGaiaV3RegisterNotificationCmd {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == RegisterNotificationCmd,
    feature: u8 == 0x06,
}

#[derive(Parse)]
struct FuStructQcGaiaV3RegisterNotificationAck {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == RegisterNotificationAck,
}

#[derive(New)]
struct FuStructQcGaiaV3UpgradeControlCmd {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == UpgradeControlCmd,
}
#[derive(Parse)]
struct FuStructQcGaiaV3UpgradeControlAck {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == UpgradeControlAck,
    status: FuQcGaiaCmdStatus == Success,
}
