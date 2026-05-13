// Copyright 2026 Sunwinon Electronics Co., Ltd.
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSunwinonHidReport {
    ChannelId = 0x61,
}

#[derive(ToString)]
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

#[derive(ToString)]
#[repr(u8)]
enum FuSunwinonFwType {
    Normal = 0x00,
    Signed = 0x10,
}

#[repr(u8)]
enum FuSunwinonDfuUpgradeMode {
    Copy = 1,
    NonCopy = 2,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSunwinonHidOut {
    report_id: FuSunwinonHidReport == ChannelId,
    device_id: u8 = 0x00,
    sub_id: u8 = 0x01,
    data_len: u16le,
    dfu_magic: u16le == 0x4744,
    dfu_cmd_type: FuSunwinonDfuCmd,
    dfu_data_len: u16le,
    data: [u8; 474],
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructSunwinonHidIn {
    report_id: FuSunwinonHidReport == ChannelId,
    device_id: u8,
    sub_id: u8,
    data_len: u16le,
    dfu_magic: u16le == 0x4744,
    dfu_cmd_type: FuSunwinonDfuCmd,
    dfu_data_len: u16le,
    data: [u8; 474],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSunwinonDfuPayloadSystemInfo {
    opcode: u8 == 0x00,
    flash_start_addr: u32le,
    len: u16le == 0x30,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSunwinonDfuPayloadProgramStart {
    mode: u8,
    image_info_raw: [u8; 40],
}

#[derive(New, Default, Setters)]
#[repr(C, packed)]
struct FuStructSunwinonDfuPayloadProgramFlash {
    write_mode: u8 == 0x01,
    dfu_save_addr: u32le,
    data_len: u16le,
    fw_data: [u8; 464],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructSunwinonDfuPayloadProgramEnd {
    end_mode: u8 == 0x01, // restart with new fw
    file_checksum: u32le,
}

#[derive(Default, Parse)]
#[repr(C, packed)]
struct FuStructSunwinonDfuRspGetInfo {
    ack_status: FuSunwinonDfuAck,
    info_that_this_program_dont_care: [u8; 19],
}

#[repr(C, packed)]
struct FuStructSunwinonDfuBootInfoFlags {
    xqspi_speed : u4,
    code_copy_mode : u1,
    system_clk : u3,
    check_image : u1,
    boot_delay : u1,
    signature_algorithm : u2,
    reserved : u20,
}

#[derive(Default, Parse)]
#[repr(C, packed)]
struct FuStructSunwinonDfuRspSystemInfo {
    ack_status: FuSunwinonDfuAck,
    opcode: u8, // unused
    start_addr: u32le,
    length: u16le == 0x30,
    bin_size: u32le,
    checksum: u32le,
    load_addr: u32le,
    run_addr: u32le,
    xqspi_xip_cmd: u32le,
    flags: FuStructSunwinonDfuBootInfoFlags,
    reserved: [u8; 24],
}

#[derive(ParseStream, Default, Setters)]
#[repr(C, packed)]
struct FuStructSunwinonDfuImageInfo {
    pattern: u16le == 0x4744,
    version: u16le,
    bin_size: u32le,
    checksum: u32le,
    load_addr: u32le,
    run_addr: u32le,
    xqspi_xip_cmd: u32le,
    flags: FuStructSunwinonDfuBootInfoFlags,
    comments: [char; 12],
    _reserved: [u8; 8],
}

#[derive(Default, Parse)]
#[repr(C, packed)]
struct FuStructSunwinonDfuRspFwInfoGet {
    ack_status: FuSunwinonDfuAck,
    dfu_save_addr: u32le,
    run_position: u8, // unused
    image_info: FuStructSunwinonDfuImageInfo,
}
