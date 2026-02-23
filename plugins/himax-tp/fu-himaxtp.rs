/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(u32)]
enum FuHimaxtpMapCount {
    Main = 9,
    Bl = 1,
}

#[repr(u32)]
enum FuHimaxtpUpdateType {
    Main,
    Bl,
}

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
    _reserved_1: u16be,
    cid: u16be,
    _reserved_2: u8,
    _reserved_3: u16be,
    _reserved_4: u8,
    _reserved_5: [u8; 12],
    _reserved_6: [u8; 12],
    _reserved_7: [u8; 12],
    _reserved_8: [u8; 12],
    _reserved_9: [u8; 12],
    _reserved_10: [u8; 12],
    vid: u16be,
    pid: u16be,
    _reserved_11: [u8; 32],
    _reserved_12: u8,
    _reserved_13: u8,
    _reserved_14: u8,
    _reserved_15: u8,
    _reserved_16: u16le,
    _reserved_17: u16le,
    _reserved_18: u8,
    _reserved_19: u8,
    _reserved_20: u8,
    _reserved_21: u16le,
    _reserved_22: u16le,
    _reserved_23: u8,
    _reserved_24: [u8; 73],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuHimaxtpIcId {
    ic_id: [u8; 12],
    vid: u16be,
    pid: u16be,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuHimaxtpIcIdMod {
    ic_id_mod: [u8; 2],
}

#[repr(u32)]
enum FuHimaxtpMapcode {
    FwCid = 0x10000000,
    FwVer = 0x10000100,
    CfgVer = 0x10000600,
    IcId = 0x10000300,
    IcIdMod = 0x10000200,
}

#[repr(u32)]
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
