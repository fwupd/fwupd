// Copyright 2025 Joe Hong <JoeHung@ilitek.com>
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
    GetFirmwareVersion = 0x40,
    GetProtocolVersion = 0x42,
    GetFirmwareId = 0x46,
    GetIcName = 0x62,
    GetIcMode = 0xC0,
    SetApMode = 0xC1,
    SetBlMode = 0xC2,
    WriteData = 0xC3,
    FlashEnable = 0xCC,
    GetBlockCrc = 0xCD,
    SetCtrlMode = 0xF0,
}

#[derive(Validate, New, Default)]
#[repr(C, packed)]
struct FuStructIlitekItsHidCmd {
    report_id: u8 == 0x03,
    // response buffer is always going to be less than 64 bytes for this hardware/plugin
    res_size_supported_id: u8 == 0xA3,
    write_len: u8 = 0,
    read_len: u8 = 0,
    cmd: FuIlitekItsCmd,
    //cmd parameters goes here
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructIlitekItsLongHidCmd {
    report_id: u8 == 0x08,
    res_size_supported_id: u8 == 0xA3,
    write_len: u16le = 0,
    read_len: u16le = 0,
    cmd: FuIlitekItsCmd,
    //cmd parameters goes here
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

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructIlitekItsCapsuleHeader {
    // capsule header
    caspule_guid: Guid == "6dcbd5ed-e82d-4c44-bda1-7194199ad92a",
    _reserve_1: [u8; 12],
    package_ver: u32le,

    // management header
    _reserve_2: [u8; 16],

    // image header
    _reserve_3: [u8; 4],
    image_guid: Guid == "116b581a-de9e-4b6f-ba6f-6af1809465a7",
    _reserve_4: [u8; 28],

    // fmp payload header
    _reserve_5: [u8; 16],
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructIlitekItsAuthHeader {
    version: u32le,
    size: u32le,
    crc: u16le,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructIlitekItsLookupHeader {
    version: u32le,
    size: u32le,
    item_size: u32le,
    cnt: u8,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructIlitekItsLookupItem {
    type: u8,
    edid: u32le,
    sensor_id: u8,
    sensor_id_mask: u8,
    fwid: u16le,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructIlitekItsSkuHeader {
    version: u32le,
    size: u32le,
    cnt: u8,
    custom_data: u32le,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructIlitekItsSkuItem {
    fwid: u16le,
    fw_ver: u64be,
    fw_size: u32le,
    fw_buf: u8,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructIlitekItsCapsule {
    header: FuStructIlitekItsCapsuleHeader,
    auth: FuStructIlitekItsAuthHeader,
    lookup: FuStructIlitekItsLookupHeader,
}
