// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuKineticDpFamily {
    Unknown,
    Mustang,
    Jaguar,
    Puma,
}

#[derive(ToString)]
enum FuKineticDpChip {
    None,
    Bobcat_2800,
    Bobcat_2850,
    Pegasus,
    Mystique,
    Dp2vga,
    Puma_2900,
    Puma_2920,
    Jaguar_5000,
    Mustang_5200,
}

enum FuKineticDpDev {
    Host,
    Port1,
    Port2,
    Port3,
//    MaxNum,
}

#[derive(ToString)]
enum FuKineticDpBank {
    A = 0,
    B = 1,
    None = 0xFF,
}

enum FuKineticDpFirmwareIdx {
    IspDrv,
    AppFw,
}

#[derive(ToString)]
enum FuKineticDpFwState {
    None,
    Irom,
    BootCode,
    App,
}

#[derive(ParseStream, Default)]
#[repr(C, packed)]
struct FuStructKineticDpPumaHeader {
    _unknown: u8,
    object_count: u8 == 8,
    // certificate + ESM + Signature + hash + certificate + Puma App + Signature + hash
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructKineticDpPumaHeaderInfo {
    type: u8,
    subtype: u8,
    length: u32le,
}

#[derive(ToString)]
enum FuKineticDpPumaMode {
    ChunkProcessed = 0x03,
    ChunkReceived = 0x07,
    FlashInfoReady = 0xA1,
    UpdateAbort = 0x55,
}

enum FuKineticDpPumaRequest {
    ChipResetRequest = 0,
    CodeLoadRequest = 0x01,
    CodeLoadReady = 0x03,
    CodeBootupDone = 0x07,
    CmdbGetinfoReq = 0xA0,
    CmdbGetinfoRead = 0xA1,
    CmdbGetinfoInvalid = 0xA2,
    CmdbGetinfoDone = 0xA3,
    FlashEraseDone = 0xE0,
    FlashRraseFail = 0xE1,
    FlashRraseRequest = 0xEE,
    FwUpdateDone = 0xF8,
    FwUpdateReady = 0xFC,
    FwUpdateRequest = 0xFE,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructKineticDpJaguarFooter {
    app_id_struct_ver: u32le,
    app_id: [u8; 4],
    app_ver_id: u32le,
    fw_ver: u16be,
    fw_rev: u8,
    customer_fw_project_id: u8,
    customer_fw_ver: u16be,
    chip_rev: u8,
    is_fpga_enabled: u8,
    reserved: [u8; 12],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructKineticDpFlashInfo {
    id: u16be,
    size: u16be,
    erase_time: u16be,
}

#[derive(ToString)]
enum FuKineticDpDpcd {
    // status
    CmdStsNone = 0x0,
    StsInvalidInfo = 0x01,
    StsCrcFailure = 0x02,
    StsInvalidImage = 0x03,
    StsSecureEnabled = 0x04,
    StsSecureDisabled = 0x05,
    StsSpiFlashFailure = 0x06,

    // command
    CmdPrepareForIspMode = 0x23,
    CmdEnterCodeLoadingMode = 0x24,
    CmdExecuteRamCode = 0x25,
    CmdEnterFwUpdateMode = 0x26,
    CmdChunkDataProcessed = 0x27,
    CmdInstallImages = 0x28,
    CmdResetSystem = 0x29,

    // other command
    CmdEnableAuxForward = 0x31,
    CmdDisableAuxForward = 0x32,
    CmdGetActiveFlashBank = 0x33,

    // 0x70 ~ 0x7F are reserved for other usage
    CmdReserved = 0x7f,
}
