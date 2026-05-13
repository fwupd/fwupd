/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(C, packed)]
struct FuStructHimaxTpHidFwUnit {
    cmd: u8,
    bin_start_offset: u16le,
    bin_size: u16le,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructHimaxTpHidInfo {
    main_mapping: [FuStructHimaxTpHidFwUnit; 9],
    bl_mapping: FuStructHimaxTpHidFwUnit,
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

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructHimaxTpIcId {
    desc: [char; 12],
    vid: u16be,
    pid: u16be,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructHimaxTpIcIdMod {
    desc: [char; 2],
}

#[repr(u32)]
enum FuHimaxTpMapcode {
    FwCid   = 0x10000000,
    FwVer   = 0x10000100,
    CfgVer  = 0x10000600,
    IcId    = 0x10000300,
    IcIdMod = 0x10000200,
}

#[derive(ToString)]
enum FuHimaxTpFwStatus {
    NoError = 0x77,
    McuE0 = 0x00,
    McuE1 = 0xA0,
    Commit = 0xB1,
    Bl = 0xB2,
    Pw = 0xB3,
    EraseFlash = 0xB4,
    FlashProgramming = 0xB5,
    NoBl = 0xC1,
    NoMain = 0xC2,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructHimaxTpMapCode {
    mcode: u32le,
    flash_addr: u32le,
    size: u32le,
    cs: u32le,
}

#[repr(u32le)]
enum FuHimaxTpRegisterAddr {
    BlockProtectBase    = 0x80000000,
    BlockProtectCmd1    = 0x80000010,
    BlockProtectCmd2    = 0x80000020,
    BlockProtectCmd3    = 0x80000024,
    BlockProtectStatus  = 0x8000002C,
    WriteProtectPin     = 0x900880BC,
}

#[derive(Getters, New)]
#[repr(C, packed)]
struct FuStructHimaxTpRegRw {
    rw_flag: u8,
    reg_addr: FuHimaxTpRegisterAddr,
    reg_value: u32le,
}

#[derive(ToString)]
enum FuHimaxTpReportId {
    Cfg                 = 0x05,
    RegRw               = 0x06,
    FwUpdate            = 0x0A,
    FwUpdateHandshaking = 0x0B,
    SelfTest            = 0x0C,
}
