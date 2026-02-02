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

enum FuNovatekTsFlashMapConst {
    FlashNormalFwStartAddr  = 0x2000,
    FlashPidAddr            = 0x3F004,
    FlashMaxSize            = 0x3C000,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuNovatekTsFlashMap {
    flash_normal_fw_start_addr: u32le,
    flash_pid_addr: u32le,
    flash_max_size: u32le,
}

enum FuNovatekTsResetState {
    ResetStateInit          = 0xA0,
    ResetStateRekBaseline   = 0xA1,
    ResetStateRekFinish     = 0xA2,
    ResetStateNormalRun     = 0xA3,
    ResetStateMax           = 0xAF,
}

enum FuNovatekTsCmdConst {
    BootResetCmd            = 0x69,
    SwResetCmd              = 0xAA,
    StopCrcCmd              = 0xA5,
}

enum FuNovatekTsFlashCmdConst {
    WriteEnable             = 0x06,
    ReadStatus              = 0x05,
    SectorErase             = 0x20,
    ResumePd                = 0xAB,
    ProgramPage             = 0x02,
    ReadData                = 0x03,
    ReadMidDid              = 0x9F,
}

enum FuNovatekTsHidConst {
    ReadReqWriteLen          = 0x000B,
}

enum FuNovatekTsGcmConst {
    CmdMagic                 = 0xC2,
}

enum FuNovatekTsMaskConst {
    StatusBusyMask           = 0x01,
    ByteMask                 = 0xFF,
    ChecksumMask             = 0xFFFF,
}

enum FuNovatekTsValueConst {
    Zero                     = 0x00,
    PidInvalidZero           = 0x0000,
    PidInvalidOnes           = 0xFFFF,
    InfoChecksumOk           = 0xFF,
}

enum FuNovatekTsGcmCodeConst {
    Enable0                 = 0x55,
    Enable1                 = 0xFF,
    Enable2                 = 0xAA,
    Disable0                = 0xAA,
    Disable1                = 0x55,
    Disable2                = 0xFF,
}

enum FuNovatekTsChecksumStatusConst {
    Ready                   = 0xAA,
    Error                   = 0xEA,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuNovatekTsMemMap {
    chip_ver_trim_addr: u32le,
    swrst_sif_addr: u32le,
    event_buf_cmd_addr: u32le,
    event_buf_hs_sub_cmd_addr: u32le,
    event_buf_reset_state_addr: u32le,
    event_map_fwinfo_addr: u32le,
    read_flash_checksum_addr: u32le,
    rw_flash_data_addr: u32le,
    enb_casc_addr: u32le,
    hid_i2c_eng_addr: u32le,
    gcm_code_addr: u32le,
    gcm_flag_addr: u32le,
    flash_cmd_addr: u32le,
    flash_cmd_issue_addr: u32le,
    flash_cksum_status_addr: u32le,
    bld_spe_pups_addr: u32le,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructNovatekTsHidReadReq {
    i2c_hid_eng_report_id: u8,
    write_len: u16le,
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

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructNovatekTsGcmCmd {
    flash_cmd: u8,
    flash_addr: u24le,
    _reserved0: u8,
    write_len: u16le,
    read_len: u16le,
    flash_checksum: u16le,
    magic: u8,
}
