// Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// device register addresses
enum FuNvtTsMemMapReg {
    ChipVerTrimAddr         = 0x1fb104,
    SwrstSifAddr            = 0x1fb43e,
    EventBufCmdAddr         = 0x130950,
    EventBufHsSubCmdAddr    = 0x130951,
    EventBufResetStateAddr  = 0x130960,
    EventMapFwinfoAddr      = 0x130978,
    ReadFlashChecksumAddr   = 0x100000,
    RwFlashDataAddr         = 0x100002,
    EnbCascAddr             = 0x1fb12c,
    HidI2cEngAddr           = 0x1fb468,
    GcmCodeAddr             = 0x1fb540,
    GcmFlagAddr             = 0x1fb553,
    FlashCmdAddr            = 0x1fb543,
    FlashCmdIssueAddr       = 0x1fb54e,
    FlashCksumStatusAddr    = 0x1fb54f,
    BldSpePupsAddr          = 0x1fb535,
    QWrCmdAddr              = 0x000000,
}

enum FuNvtTsFlashMapConst {
    FlashNormalFwStartAddr  = 0x2000,
    FlashPidAddr            = 0x3f004,
    FlashMaxSize            = 0x3c000,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructNvtTsHidReadReq {
    i2c_hid_eng_report_id: u8,
    write_len: u16le,
    i2c_eng_addr_0: u8,
    i2c_eng_addr_1: u8,
    i2c_eng_addr_2: u8,
    target_addr_0: u8,
    target_addr_1: u8,
    target_addr_2: u8,
    _reserved0: u8,
    len: u16le,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructNvtTsHidWriteHdr {
    i2c_hid_eng_report_id: u8,
    write_len: u16le,
    target_addr_0: u8,
    target_addr_1: u8,
    target_addr_2: u8,
}
