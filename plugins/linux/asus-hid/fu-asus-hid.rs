// Copyright 2024 Mario Limonciello <superm1@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuAsusHidController {
    Primary,
    Main,
}

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

#[derive(Default, New)]
#[repr(C, packed)]
struct FuStructAsusManCommand {
    report_id: FuAsusHidReportId == Info,
    data: [char; 14] == "ASUS Tech.Inc.",
    terminator: u8 == 0,
}

#[derive(Default, New, Getters)]
#[repr(C, packed)]
struct FuStructAsusManResult {
    report_id: FuAsusHidReportId == Info,
    data: [char; 31],
}

#[derive(Default, New)]
#[repr(C, packed)]
struct FuStructAsusHidCommand {
    report_id: FuAsusHidReportId == Info,
    cmd: u32le,
    length: u8,
}

#[derive(Default, New)]
#[repr(C, packed)]
struct FuStructAsusHidResult {
    report_id: FuAsusHidReportId == Info,
    data: [u8; 31],
}

#[derive(Getters, ParseStream)]
#[repr(C, packed)]
struct FuStructAsusHidDesc {
    fga: [char; 8],
    reserved: u8,
    product: [char; 6],
    reserved: u8,
    version: [char; 8],
    reserved: u8,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructAsusHidFwInfo {
    header: FuStructAsusHidCommand,
    reserved: u8,
    description: FuStructAsusHidDesc,
}

#[derive(Default, New)]
#[repr(C, packed)]
struct FuStructAsusPreUpdateCommand {
    report_id: FuAsusHidReportId == Info,
    cmd: u32le,
    length: u8,
    data: [u8; 58],
}

// Flashing sequence

#[derive(Default, New)]
#[repr(C, packed)]
struct FuStructAsusFlashReset {
    command: u8 == 0xc4,
    reserved: [u8; 62],
}

#[derive(Default, New, Getters)]
#[repr(C, packed)]
struct FuStructAsusReadFlashCommand {
    command: u8 == 0xd1,
    offset: u24le,
    datasz: u8,
    data: [u8; 58],
}
