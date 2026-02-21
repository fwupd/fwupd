/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

enum FuHimaxtpMapCount {
    Main = 9,
    Bl = 1,
}

enum FuHimaxtpUpdateType {
    Main,
    Bl,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuHimaxtpHidFwUnit {
    cmd: u8,
    bin_start_offset: u16le,
    bin_size: u16le,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuHimaxtpHidInfo {
    main_mapping: [FuHimaxtpHidFwUnit; 9],
    bl_mapping: FuHimaxtpHidFwUnit,
    passwd: u16be,
    cid: u16be,
    panel_ver: u8,
    fw_ver: u16be,
    ic_sign: u8,
    customer: [char; 12],
    project: [char; 12],
    fw_major: [char; 12],
    fw_minor: [char; 12],
    date: [char; 12],
    ic_sign_2: [char; 12],
    vid: u16be,
    pid: u16be,
    cfg_info: [u8; 32],
    cfg_version: u8,
    disp_version: u8,
    rx: u8,
    tx: u8,
    yres: u16le,
    xres: u16le,
    pt_num: u8,
    mkey_num: u8,
    pen_num: u8,
    pen_yres: u16le,
    pen_xres: u16le,
    ic_num: u8,
    debug_info: [u8; 73],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuHimaxtpIcId {
    ic_id: [char; 12],
    vid: u16be,
    pid: u16be,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuHimaxtpIcIdMod {
    ic_id_mod: [char; 2],
}


enum FuHimaxtpMapcode {
    FwCid = 0x10000000,
    FwVer = 0x10000100,
    CfgVer = 0x10000600,
    IcId = 0x10000300,
    IcIdMod = 0x10000200,
}

enum FuHimaxtpUpdateErrorCode {
    NoError = 0x77,
    McuE0 = 0x00,
    McuE1 = 0xA0,
    NoBl = 0xC1,
    NoMain = 0xC2,
    Bl = 0xB2,
    Pw = 0xB3,
    EraseFlash = 0xB4,
    FlashProgramming = 0xB5,
    NoDevice = 0xFFFFFF00,
    LoadFwBin = 0xFFFFFF01,
    Initial = 0xFFFFFF02,
    PollingTimeout = 0xFFFFFF03,
    PollingAgain = 0xFFFFFF04,
    FwTransfer = 0xFFFFFF05,
    FwEntryInvalid = 0xFFFFFF06,
    FlashProtect = 0xFFFFFF07,
}
