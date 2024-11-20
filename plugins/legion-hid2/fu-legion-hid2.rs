// Copyright 2024 Mario Limonciello <superm1@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Commands sent ove rXInput, DInput
#[repr(u32)]
enum FuLegionHid2Command {
    GetVersion = 0x01,
    GetMcuId = 0x02,
    StartIap = 0xE1,
    IcReset = 0xEF,
}

#[repr(u8)]
enum FuLegionHid2ReportId {
    Iap = 0x1,
    Communication = 0x4,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructLegionGetVersion {
    cmd: u8 == 0x01,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructLegionVersion {
    command: u8,
    version: u32le,
    reserved: [u8; 59],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructLegionGetMcuId {
    cmd: u8 == 0x02,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructLegionMcuId {
    id: [u8; 12],
    reserved: [u8; 52],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructLegionStartIap {
    cmd: u8 == 0xE1,
    data: [char; 7] == "UPGRADE",
    reserved: [u8; 57],
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructLegionIapResult {
    ret: u8,
    reserved: [u8; 63],
}

//Reset in app mode, unused by fwupd
//#[derive(New)]
//#[repr(C, packed)]
//struct FuStructLegionIcReset {
//    cmd: u8 == 0xEF,
//    data: [char; 7] == "ICRESET",
//    reserved: [u8; 57],
//}

// IAP commands
enum FuLegionIapHostTag {
    IapUnlock = 0x5A80,
    IapData = 0x5A81,
    IapSignature = 0x5A82,
    IapUpdate = 0x5A83,
    IapCarry = 0x5A84,
    IapVerify = 0x5A85,
    IapRestart = 0x5A86,
}

enum FuLegionIapDeviceTag {
    IapAck = 0xA510,
}

enum FuLegionIapError {
    IapOk = 0x00,
    IapErr = 0x01,
    IapCertified = 0x02,
    IapNotCertified = 0x03,
}

#[derive(New, Setters, Getters)]
#[repr(C, packed)]
struct FuStructLegionIapTlv {
    tag: u16le,
    length: u16le,
    value: [u8; 60],
}

// Parsing firmware update header
#[derive(Getters, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructLegionHid2Header {
    magic: [char; 7] == "#Legion",
    reserved: [u8; 7],
    sig_add: u32le,
    sig_len: u32le,
    data_add: u32le,
    data_len: u32le,
}

#[derive(Getters, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructLegionHid2Version {
    signature: [char; 7] == "VERSION",
    reserved: u8,
    version: u32le,
}
