// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSunwinonHidReport {
    channel_id = 0x61,
}

#[derive(New, Default, Getters)]
#[repr(C, packed)]
struct FuStructSunwinonHidOut {
    report_id: FuSunwinonHidReport == channel_id,
    device_id: u8 = 0x00,
    sub_id: u8 = 0x01,
    data_len: u16le,
    data: [u8; 480],
}

#[derive(Default, New, Validate, Getters)]
#[repr(C, packed)]
struct FuStructSunwinonHidIn {
    report_id: FuSunwinonHidReport == channel_id,
    device_id: u8,
    sub_id: u8,
    data_len: u16le,
    data: [u8; 480],
}

#[repr(u8)]
enum FuSunwinonDfuEvent {
    FrameCheckError,
    ImgInfoCheckFail,
    ImgInfoLoadAddrError,
    GetInfoFail,
    ProStartError,
    ProStartSuccess,
    ProFlashSuccess,
    ProFlashFail,
    ProEndSuccess,
    ProEndFail,
    EraseStartSuccess,
    EraseSuccess,
    EraseEndSuccess,
    EraseRegionNotAligned,
    EraseRegionOverlap,
    EraseFlashFail,
    EraseRegionNotExist,
    FastDfuProFlashSuccess,
    FastDfuFlashFail,
    DfuFwSaveAddrConflict,
    DfuAckTimeout,
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
