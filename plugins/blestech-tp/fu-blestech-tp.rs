/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(u8)]
enum FuBlestechTpCmd {
    GetChecksum = 0x3f,
    GetFwVer = 0xb6,
    UpdateStartReq = 0xb1,
    ProgramPage = 0xb2,
    ProgramPageEnd = 0xb3,
    ProgramChecksum = 0xb4,
    ProgramEnd = 0xb5,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechTpSetHdr {
    report_id: u8,
    pack_len: u8,
    checksum: u8,
    frame_flag: u8 == 0,
    write_len: u16be,
    read_len: u16be,
    data: [u8; 25],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechTpUpdateStartReq {
    cmd: FuBlestechTpCmd == UpdateStartReq,
    magic_num: u64be == 0x7565554563756933,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechTpSwitchBootReq {
    cmd: u32be == 0xffff5aa5,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechTpGetFwVerReq {
    cmd: FuBlestechTpCmd == GetFwVer,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuBlestechTpGetFwVerRes {
    val: u16be,
    _reserved: [u8; 5],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechTpProgramEndReq {
    cmd: FuBlestechTpCmd == ProgramEnd,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechTpProgramChecksumReq {
    cmd: FuBlestechTpCmd == ProgramChecksum,
    val: u16le,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuBlestechTpProgramChecksumRes {
    _unknown: u8,
    val: u16le,
    _reserved: [u8; 1],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechTpProgramPageEndReq {
    cmd: FuBlestechTpCmd == ProgramPageEnd,
    page: u16le,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuBlestechTpProgramPageEndRes {
    _unknown: u8,
    checksum: u8,
    _reserved: [u8; 1],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechTpProgramPageReq {
    cmd: FuBlestechTpCmd == ProgramPage,
    data: [u8; 24],
}
