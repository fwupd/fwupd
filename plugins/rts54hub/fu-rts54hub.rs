// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuRts54hubRtd21xxIspStatus {
    Busy = 0xbb,        // host must wait for device
    IdleSuccess = 0x11, // previous command was OK
    IdleFailure = 0x12, // previous command failed
}

enum FuRts54hubI2cSpeed {
    100K,
    200K,
    300K,
    400K,
    500K,
    600K,
    700K,
    800K,
}

#[repr(u8)]
#[derive(Bitfield, ToString)]
enum FuRts54hubVendorCmd {
    None = 0,
    Enable = 1 << 0,
    AccessFlash = 1 << 1,
}

enum FuRts54hubRtd21xxBgIspCmd {
    None,
    FwUpdateStart,
    FwUpdateIspDone,
    GetFwInfo,
    FwUpdateExit,
    GetProjectIdAddr,
    SyncIdentifyCode,
}

enum FuRts54hubRtd21xxFgIspCmd {
    None,
    EnterFwUpdate,
    GetProjectIdAddr,
    SyncIdentifyCode,
    GetFwInfo,
    FwUpdateStart,
    FwUpdateIspDone,
    FwUpdateReset,
    FwUpdateExit,
}

#[repr(u8)]
enum FuRts54HubMergeInfoDdcciOpcode {
    Communication = 0x11,
    DdcciToDebug = 0x55,
    First = 0x77,
    GetVersion = 0x99,
    SetVersion = 0xBB,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructRts54HubDdcPkt {
    first_opcode: FuRts54HubMergeInfoDdcciOpcode = First,
    second_opcode: u8,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructRts54HubDdcWriteMergeInfoPkt {
    first_opcode: FuRts54HubMergeInfoDdcciOpcode = First,
    second_opcode: u8,
    major_version: u8,
    minor_version: u8,
    patch_version: u8,
    build_version: u8,
}
