// Copyright 2024 Mario Limonciello <superm1@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// these are called with report ID 0x5A
#[repr(u32)]
enum FuAsusHidCommand {
    InitSequence = 0x010101d1, // No idea
    PreUpdate =  0x200005,
    PreUpdate2 = 0x200907, //packet 265 in ally-x (Get report response in 268)
    PreUpdate3 = 0x200908, //packet 269 in ally-x
    PreUpdate4 = 0x260b07, //packet 271 in ally-x (Get report response in 274)
    PreUpdate5 = 0x260b08, //packet 275 in ally-x
    PreUpdate6 = 0x9332e0,
    GetFwConfig = 0x312005, // OEM_GetFWConfig
    FwVersion = 0x00310305, // OEM_GetFWVersion
    MainFwVersion = 0x00310405,  // OEM_GetMainFWVersion
    FlashTaskSomething = 0xc000,
    FlashTaskSomething2 = 0xd000,
    SwitchToRom = 0xd2, // OEM_MainSwitchToRom, doesn't appear in packet cap
}

//Info stuff happens on report ID 0x5A
//Flashing seems to happen on report ID 0x0

#[repr(u8)]
enum FuAsusHidReportId {
    Flashing = 0x00,
    Info = 0x5A,
}

#[derive(New)]
struct FuStructAsusManCommand {
    report_id: FuAsusHidReportId == Info,
    data: [char; 14] == "ASUS Tech.Inc.",
    terminator: u8 == 0,
}

#[derive(New, Getters)]
struct FuStructAsusManResult {
    report_id: FuAsusHidReportId == Info,
    data: [char; 31],
}

#[derive(New, Getters)]
struct FuStructAsusHidCommand {
    report_id: FuAsusHidReportId == Info,
    cmd: u32,
    length: u8,
}

#[derive(New, Getters)]
struct FuStructAsusHidResult {
    report_id: FuAsusHidReportId == Info,
    data: [u8; 31],
}

#[derive(New, Getters, ParseStream)]
struct FuStructAsusHidDesc {
    fga: [char; 8],
    reserved: u8,
    product: [char; 6],
    reserved: u8,
    version: [char; 8],
    reserved: u8,
}

#[derive(New, Parse)]
struct FuStructAsusHidFwInfo {
    header: FuStructAsusHidCommand,
    reserved: u8,
    description: FuStructAsusHidDesc,
}

#[derive(New, Getters)]
struct FuStructAsusPreUpdateCommand {
    report_id: FuAsusHidReportId == Info,
    cmd: u32,
    length: u8,
    data: [u8; 58],
}

// Flashing sequence

#[repr(u8)]
enum FuAsusHidFlashCommand {
    IdentifyFlash = 0x070020f0,
    ReadFlash = 0xd1,
    ClearRemoteBuffer = 0xc0,
    WritePage = 0xc1,
    Flash4 = 0xc2,
    FlushPage = 0xc3,
    VerifyPage = 0xd0,
    FlashReset = 0xc4,
}

#[derive(New, Getters)]
struct FuStructFlashIdentify {
    command: u32 == 0x070020f0,
    datasz: u8 == 0x2,
}

#[derive(New, Getters)]
struct FuStructFlashIdentifyResponse {
    command: u32,
    datasz: u8,
    part: u16,
}

#[derive(New, Getters, Setters)]
struct FuStructAsusReadFlashCommand {
    command: u8 == 0xd1,
    offset: u24,
    datasz: u8,
    data: [u8; 58],
}

#[derive(New, Getters, Setters)]
struct FuStructAsusWriteFlashCommand {
    command: u8 == 0xc1,
    offset: u16,
    datasz: u8,
    data: [u8; 59],
}

#[derive(New)]
struct FuStructAsusFlashReset {
    command: u8 == 0xc4,
    reserved: [u8; 62],
}

#[derive(New)]
struct FuStructAsusFlushPage {
    command: u8 == 0xc3,
    address: u32,
    page_size: u16 == 0x400,
}

#[derive(New)]
struct FuStructAsusClearBuffer {
    command: u8 == 0xc0,
    reserved: [u8; 62],
}

#[derive(New)]
struct FuStructAsusVerifyBuffer {
    command: u8 == 0xd0,
    reserved: [u8; 62],
}

#[derive(New, Getters)]
struct FuStructAsusVerifyResult {
    command: u8 == 0xd0,
    datasz: u8 == 0x6,
}

// Wire shark filter
// !(usbhid.setup.ReportID == 13) && _ws.col.protocol == "USBHID" && !(_ws.col.info == "SET_IDLE Request") && !(_ws.col.info == "SET_IDLE Response")
