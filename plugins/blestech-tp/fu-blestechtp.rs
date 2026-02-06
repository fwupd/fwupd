/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

enum FuBlestechtpCmd {
    GetChecksum = 0x3f,
    GetFwVer = 0xb6,
    ProgramPage = 0xb2,
    ProgramPageEnd = 0xb3,
    ProgramChecksum = 0xb4,
    ProgramEnd = 0xb5,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechtpSetHdr {
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
struct FuBlestechtpUpdateStart {
    cmd: u8 == 0xb1,
    magic_num: u64be == 0x7565554563756933,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuBlestechtpSwitchBoot {
    cmd: u32be == 0xffff5aa5,
}
