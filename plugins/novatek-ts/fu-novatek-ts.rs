// Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// device register addresses
enum FuNovatekTsMemMapReg {
    ChipVerTrimAddr         = 0x1FB104,
    SwrstSifAddr            = 0x1FB43E,
    EventBufCmdAddr         = 0x130950,
    EventBufHsSubCmdAddr    = 0x130951,
    EventBufResetStateAddr  = 0x130960,
    EventMapFwinfoAddr      = 0x130978,
    ReadFlashChecksumAddr   = 0x100000,
    RwFlashDataAddr         = 0x100002,
    EnbCascAddr             = 0x1FB12C,
    HidI2cEngAddr           = 0x1FB468,
    GcmCodeAddr             = 0x1FB540,
    GcmFlagAddr             = 0x1FB553,
    FlashCmdAddr            = 0x1FB543,
    FlashCmdIssueAddr       = 0x1FB54E,
    FlashCksumStatusAddr    = 0x1FB54F,
    BldSpePupsAddr          = 0x1FB535,
}

enum FuNovatekTsResetState {
    ResetStateInit          = 0xA0,
    ResetStateRekBaseline   = 0xA1,
    ResetStateRekFinish     = 0xA2,
    ResetStateNormalRun     = 0xA3,
    ResetStateMax           = 0xAF,
}

enum FuNovatekTsCmd {
    BootReset               = 0x69,
    SwReset                 = 0xAA,
    StopCrc                 = 0xA5,
}

enum FuNovatekTsChecksumStatus {
    Ready                   = 0xAA,
    Error                   = 0xEA,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructNovatekTsHidReadReq {
    i2c_hid_eng_report_id: u8,
    write_len: u16le == 0x000B,
    i2c_eng_addr: u24le,
    target_addr: u24le,
    _reserved0: u8,
    len: u16le,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructNovatekTsHidWriteHdr {
    i2c_hid_eng_report_id: u8,
    write_len: u16le,
    target_addr: u24le,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructNovatekTsGcmCmd {
    flash_cmd: u8,
    flash_addr: u24le,
    _reserved0: u8,
    write_len: u16le,
    read_len: u16le,
    flash_checksum: u16le,
    magic: u8 == 0xC2,
}

