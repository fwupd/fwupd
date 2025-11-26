// Copyright 2025 Joe Hong <joe_hung@ilitek.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuIlitekItsCtrlMode {
    Normal = 0x0,
    Test = 0x1,
    Debug = 0x2,
    Suspend = 0x3,
}

#[repr(u8)]
enum FuIlitekItsCmd {
    GetSensorId = 0x27,
    GetFirmwareVersion = 0x40,
    GetProtocolVersion = 0x42,
    GetFirmwareId = 0x46,
    GetMcuVersion = 0x61,
    GetMcuInfo = 0x62,
    GetIcMode = 0xC0,
    SetApMode = 0xC1,
    SetBlMode = 0xC2,
    WriteData = 0xC3,
    FlashEnable = 0xCC,
    GetBlockCrc = 0xCD,
    SetCtrlMode = 0xF0,
}

#[derive(Parse, Default)]
struct FuStructIlitekItsHidRes {
    report_id: u8 == 0x03,
    res_size_supported_id: u8 == 0xA3,
    cmd: u8,
    read_len: u8,
    data: [u8; 60],
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructIlitekItsHidCmd {
    report_id: u8 == 0x03,
    res_size_supported_id: u8 == 0xA3,
    write_len: u8 = 0,
    read_len: u8 = 0,
    cmd: FuIlitekItsCmd,
    data: [u8; 59],
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructIlitekItsLongHidCmd {
    report_id: u8 == 0x08,
    res_size_supported_id: u8 == 0xA3,
    write_len: u16le = 0,
    read_len: u16le = 0,
    cmd: FuIlitekItsCmd,
    data: [u8; 1024],
}

#[derive(Getters)]
#[repr(C, packed)]
struct FuStructIlitekItsFwid {
    customer_id: u16le,
    fwid: u16le,
}

#[derive(Getters, Default)]
#[repr(C, packed)]
struct FuStructIlitekItsSensorId {
    header: u16le == 0x5aa5,
    sensor_id: u8,
}

#[derive(Getters)]
#[repr(C, packed)]
struct FuStructIlitekItsMcuVersion {
    ic_name: u16le,
    data_flash_addr: u24le,
    data_flash_size: u8,
    module_name: [char; 26],
}

#[derive(Getters)]
#[repr(C, packed)]
struct FuStructIlitekItsMcuInfo {
    ic_name: [char; 5],
    _reserve_1: [u8; 2],
    mm_addr: u24le,
    module_name: [char; 18],
}

#[repr(C, packed)]
struct FuStructIlitekItsBlockInfo {
    addr: u24le,
}

#[derive(ParseBytes, Getters)]
#[repr(C, packed)]
struct FuStructIlitekItsMmInfo {
    mapping_ver: u24le,
    protocol_ver: u24le,
    ic_name: [u8; 6],

    tuning_ver: u32le,
    fw_ver: u32le,
    _reserve_1: [u8; 24],
    fwid: u16le,
    _reserve_2: [u8; 34],
    block_num: u8,
    _reserve_3: [u8; 3],
    blocks: [FuStructIlitekItsBlockInfo; 10],
    _reserve_4: [u8; 9],
    end_addr: u24le,
    _reserve_5: [u8; 2],
}
