// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSunwinonHidReport {
    ReportId = 0x61,
    ReportDataLen = 480,
}

#[derive(New, Default, Getters)]
#[repr(C, packed)]
struct FuStructSunwinonHidOut {
    report_id: FuSunwinonHidReport == ReportId,
    device_id: u8 = 0x00,
    sub_id: u8 = 0x01,
    data_len: u16le,
    data: [u8; 480],
}

#[derive(Default, New, Validate, Getters)]
#[repr(C, packed)]
struct FuStructSunwinonHidIn {
    report_id: FuSunwinonHidReport == ReportId,
    device_id: u8,
    sub_id: u8,
    data_len: u16le,
    data: [u8; 480],
}

#[repr(u8)]
enum FuSunwinonDfuEraseStatus {
    RegionNotAligned = 0x00,
    StartSuccess = 0x01,
    Success = 0x02,
    EndSuccess = 0x03,
    RegionsOverlap = 0x04,
    Fail = 0x05,
    RegionsNotExist = 0x06,
}

#[repr(u8)]
enum FuSunwinonDfuCmdType {
    FastDfuFlashSuccess = 0xFF,
}

#[repr(u8)]
enum FuSunwinonDfuEvent {
    FrameCheckError = 0x00,
    ImgInfoCheckFail = 0x01,
    ImgInfoLoadAddrError = 0x02,
    GetInfoFail = 0x03,
    ProStartError = 0x04,
    ProStartSuccess = 0x05,
    ProFlashSuccess = 0x06,
    ProFlashFail = 0x07,
    ProEndSuccess = 0x08,
    ProEndFail = 0x09,
    EraseStartSuccess = 0x0A,
    EraseSuccess = 0x0B,
    EraseEndSuccess = 0x0C,
    EraseRegionNotAligned = 0x0D,
    EraseRegionOverlap = 0x0E,
    EraseFlashFail = 0x0F,
    EraseRegionNotExist = 0x10,
    FastDfuProFlashSuccess = 0x11,
    FastDfuFlashFail = 0x12,
    DfuFwSaveAddrConflict = 0x13,
    DfuAckTimeout = 0x14,
}

#[repr(u32)]
enum FuSunwinonDfuConfig {
    // The flash start address of peripheral
    PepherialFlashStartAddr = 0x0020_0000,
    // Flash program length for one frame
    OnceProgramLen = 464,
    // Maximum length of a single logical transmission in async mode
    SendSizeMax = 517,
    // DFU master wait ACK timeout (ms)
    AckWaitTimeout = 4000,
    // DFU peripheral chip reset time (ms)
    PepherialResetTime = 2000,
}

#[repr(u8)]
enum FuSunwinonDfu {
    Version = 0x02,
}

#[repr(u8)]
enum FuSunwinonFastDfuMode {
    Disable = 0x00,
    Enable = 0x02,
}

#[repr(u8)]
enum FuSunwinonDfuUpgradeMode {
    Copy = 1,
    NonCopy = 2,
}

#[repr(u32)]
enum FuSunwinonDfuFw {
    EncOrSignPattern = 0xDEAD_BEEF,
    SignPattern = 0x4E47_4953, // "SIGN"
}

#[repr(u32)]
enum FuSunwinonDfuFrameMax {
    Tx = 479, // OnceProgramLen + overhead 15
    Rx = 64,
}
