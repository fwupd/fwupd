// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuLogitechTapTouchDirection {
    Out = 0x0,
    In = 0x0,
}

#[repr(u8)]
enum FuStructLogitechTapTouchHidCmd {
    GetFirmwareVersion = 0x40,
    GetProtocolVersion = 0x42,
    GetMcuVersion = 0x61,
    GetSysBusyStatus = 0x80,
    GetMcuMode = 0xC0,
    SetApMode = 0xC1,
    SetBlMode = 0xC2,
    WriteData = 0xC3,
    WriteEnable = 0xC4,
    GetApCrc = 0xC7,
    GetDfCrc = 0xCA,
    SetTdeTestMode = 0xF2,
}

#[derive(New)]
struct FuStructLogitechTapTouchHidReq {
    report_id: u8 == 0x03,
    // response buffer is always going to be less than 64 bytes for this hardware/plugin
    // if response buffer is more than 64 bytes (for some hardware/interfaces), then use A4 here 
    res_size_supported_id: u8 == 0xA3,
    payload_len: u8,
    // bytes to read from response 
    response_len: u8,
    cmd: FuStructLogitechTapTouchHidCmd,
    //payload goes here
}
