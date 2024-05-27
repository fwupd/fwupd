// Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

//enum FuQcGaiaV3Type {
//    Command = 0x0b00,
//    Notification = 0x0b01,
//    Response = 0x0b10,
//    Error = 0x0b11,
//}

#[derive(ToString)]
#[repr(u16be)]
enum FuQcGaiaV3Type {
    Command = 0x0000, // 00
    Notification = 0x0080, //01
    Response = 0x0100, //10
    Error = 0x00180, //11
}

#[repr(u8)]
enum FuQcGaiaV3Features {
    Core = 0x00,
    Dfu = 0x06,
}

// Commands: 80-CH482-1, 80-CF422-1, 80-CF378-1

#[derive(ToString)]
#[repr(u16be)]
enum FuQcGaiaV2Cmd {
    GetApiReq = 0x0300,
    GetApiResp = 0x8300,
    // GetModuleReq = 0x0303,
    // GetModuleResp = 0x8303,
    // GetAppVersionReq = 0x0304,
    // GetAppVersionResp = 0x8304,
    UpgradeConnectCmd = 0x0640,
    UpgradeConnectAck = 0x8640,
    UpgradeDisconnectCmd = 0x0641,
    UpgradeDisconnectAck = 0x8641,
    UpgradeControlCmd = 0x0642,
    UpgradeControlAck = 0x8642,
}

#[derive(ToString)]
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
    GetAppVersionReq = 0x0005,
    GetAppVersionResp = 0x0105,
    GetTransportInfoReq = 0x000c,
    GetTransportInfoResp = 0x010c,
    SetTransportInfoReq = 0x000d,
    SetTransportInfoResp = 0x010d,
    GetSystemInfoReq = 0x0011,
    GetSystemInfoResp = 0x0111,
    UpgradeConnectCmd = 0x0600,
    UpgradeConnectAck = 0x0780,
    UpgradeDisconnectCmd = 0x0601,
    UpgradeDisconnectAck = 0x0781,
    UpgradeControlCmd = 0x0602,
    UpgradeControlAck = 0x0782,
 }

#[derive(ToString)]
#[repr(u8)]
enum FuQcGaiaCmdStatus {
    Success = 0x00,
    NotSupported = 0x01,
    InsufficientResources = 0x03,
    InvalidParameter = 0x05,
    IncorrectState = 0x06,
    InProgerss = 0x07,
}


#[derive(New,Parse)]
struct FuStructQcGaiaV3Hdr {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd, // 15-9b featureId, 8-7b type, 6-0b commandId
}

#[derive(New)]
struct FuStructQcGaiaV2ApiReq {
    vendorId: u16be,
    command: FuQcGaiaV2Cmd == GetApiReq,
}
#[derive(Parse)]
struct FuStructQcGaiaV2Api {
    vendorId: u16be,
    command: FuQcGaiaV2Cmd == GetApiResp,
    status: FuQcGaiaCmdStatus == Success,
    protocol: u8,
    major: u8,
    minor: u8,
}

// #[derive(New)]
// struct FuStructQcGaiaV2ModuleReq {
//     vendorId: u16be,
//     command: FuQcGaiaV2Cmd == GetModuleReq,
// }
// #[derive(Parse)]
// struct FuStructQcGaiaV2Module {
//     vendorId: u16be,
//     command: FuQcGaiaV2Cmd == GetModuleResp,
//     status: FuQcGaiaCmdStatus == Success,
//     hwId: u16be,
//     designId: u16be,
//     moduleId: u32be,
// }
//
// #[derive(New)]
// struct FuStructQcGaiaV2AppVersionReq {
//     vendorId: u16be,
//     command: FuQcGaiaV2Cmd == GetAppVersionReq,
// }
// #[derive(Parse)]
// struct FuStructQcGaiaV2AppVersion {
//     vendorId: u16be,
//     command: FuQcGaiaV2Cmd == GetAppVersionResp,
//     // variable string
// }
//
// #[derive(New)]
// struct FuStructQcGaiaV2ConnectReq {
//     vendorId: u16be,
//     command: FuQcGaiaV2Cmd == UpgradeConnectReq,
// }
// #[derive(Parse)]
// struct FuStructQcGaiaV2Connect {
//     vendorId: u16be,
//     command: FuQcGaiaV2Cmd == UpgradeConnectResp,
// }
//
// #[derive(New)]
// struct FuStructQcGaiaV2DisconnectReq {
//     vendorId: u16be,
//     command: FuQcGaiaV2Cmd == UpgradeDisconnectReq,
// }
// #[derive(Parse)]
// struct FuStructQcGaiaV2Disconnect {
//     vendorId: u16be,
//     command: FuQcGaiaV2Cmd == UpgradeDisconnectResp,
// }


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
struct FuStructQcGaiaV3AppVersionReq {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetAppVersionReq,
}
#[derive(Parse)]
struct FuStructQcGaiaV3AppVersion {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetAppVersionResp,
    // variable string
}

#[derive(New)]
struct FuStructQcGaiaV3SystemInfoReq {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetSystemInfoReq,
    req: u8 == 0x00, // TBD: only one is supported atm
    offset: u16be == 0x00,
    reserve: u8 == 0x00,
}
#[derive(Parse)]
struct FuStructQcGaiaV3SystemInfo {
    vendorId: u16be,
    command: FuQcGaiaV3Cmd == GetSystemInfoResp,
    moreData: FuQcMore,
    req: u8 == 0x00,
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
struct FuStructQcGaiaV2UpgradeConnectCmd {
    vendorId: u16be,
    command: FuQcGaiaV2Cmd == UpgradeConnectCmd,
}
#[derive(Parse)]
struct FuStructQcGaiaV2UpgradeConnectAck {
    vendorId: u16be,
    command: FuQcGaiaV2Cmd == UpgradeConnectAck,
    status: FuQcGaiaCmdStatus == Success,
}

#[derive(New)]
struct FuStructQcGaiaV2UpgradeDisconnectCmd {
    vendorId: u16be,
    command: FuQcGaiaV2Cmd == UpgradeDisconnectCmd,
}
#[derive(Parse)]
struct FuStructQcGaiaV2UpgradeDisconnectAck {
    vendorId: u16be,
    command: FuQcGaiaV2Cmd == UpgradeDisconnectAck,
    status: FuQcGaiaCmdStatus == Success,
}

#[derive(New)]
struct FuStructQcGaiaV2UpgradeControlCmd {
    vendorId: u16be,
    command: FuQcGaiaV2Cmd == UpgradeControlCmd,
}
#[derive(Parse)]
struct FuStructQcGaiaV2UpgradeControlAck {
    vendorId: u16be,
    command: FuQcGaiaV2Cmd == UpgradeControlAck,
    status: FuQcGaiaCmdStatus == Success,
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
    status: FuQcGaiaCmdStatus == Success,
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
    status: FuQcGaiaCmdStatus == Success,
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

