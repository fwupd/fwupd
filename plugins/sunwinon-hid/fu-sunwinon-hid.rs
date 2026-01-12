// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSunwinonHidReport {
    ChannelId = 0x61,
}

#[repr(u16le)]
enum FuSunwinonDfuCmd {
    GetInfo = 0x0001,
    ProgramStart = 0x0023,
    ProgramFlash = 0x0024,
    ProgramEnd = 0x0025,
    SystemInfo = 0x0027,
    ModeSet = 0x0041,
    FwInfoGet = 0x0042,
    FastDfuFlashSuccess = 0x00FF,
}

#[repr(u8)]
enum FuSunwinonDfuAck {
    Success = 0x01,
    Error = 0x02,
}

#[derive(New, Default, Validate, Getters, Setters)]
#[repr(C, packed)]
struct FuStructSunwinonDfuFrameHeader {
    header: u16le == 0x4744,
    cmd_type: FuSunwinonDfuCmd,
    data_len: u16le,
}

#[derive(New, Default, Setters)]
#[repr(C, packed)]
struct FuStructSunwinonHidOutV2 {
    report_id: FuSunwinonHidReport == ChannelId,
    device_id: u8 = 0x00,
    sub_id: u8 = 0x01,
    data_len: u16le,
    dfu_header : FuStructSunwinonDfuFrameHeader,
    data: [u8; 474],
}

#[derive(New, Default, Validate, Getters)]
#[repr(C, packed)]
struct FuStructSunwinonHidInV2 {
    report_id: FuSunwinonHidReport == ChannelId,
    device_id: u8,
    sub_id: u8,
    data_len: u16le,
    dfu_header : FuStructSunwinonDfuFrameHeader,
    data: [u8; 474],
}

#[derive(New, Default, Validate, Getters)]
#[repr(C, packed)]
struct FuStructSunwinonDfuRspFwInfoGet {
    ack_status: FuSunwinonDfuAck,
    dfu_save_addr: u32le,
    run_position: u8, // currently unused
    image_info_raw: [u8; 40],
    padding: [u8; 8],
}

// legacy code below

#[derive(New, Default, Getters)]
#[repr(C, packed)]
struct FuStructSunwinonHidOut {
    report_id: FuSunwinonHidReport == ChannelId,
    device_id: u8 = 0x00,
    sub_id: u8 = 0x01,
    data_len: u16le,
    data: [u8; 480],
}

#[derive(New, Default, Validate, Getters)]
#[repr(C, packed)]
struct FuStructSunwinonHidIn {
    report_id: FuSunwinonHidReport == ChannelId,
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
