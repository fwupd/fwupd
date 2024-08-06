// Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ValidateStream, ParseStream)]
struct FuStructTelinkDfuHdr {
    magic_common: u32le == 0x12345678,
    magic_fwinfo: u32le == 0x13572468,
    magic: u32le,
    _reserved1: u32le,
    _reserved2: u32le,
    version: u32le,
}

//#[derive(ToString)]
enum FuTelinkDfuState {
    Inactive,
    Active,
    Storing,
    Cleaning,
}

#[derive(New, Getters)]
struct FuStructTelinkDfuHidReport {
    report_id: u8,
    _reserved: [u8; 4],
    perhaps_data: [u8; 25],
}

enum FuTelinkDfuCmd {
    OtaFwVersion    = 0xff00,
    OtaStart        = 0xff01,
    OtaEnd          = 0xff02,
    OtaStartReq     = 0xff03,
    OtaStartRsp     = 0xff04,
    OtaTest         = 0xff05,
    OtaTestRsp      = 0xff06,
    OtaError        = 0xff07,
}

#[derive(New, Getters)]
struct FuStructTelinkDfuBlePkt {
    preamble: u16,
    payload: [u8; 16] = 0xFF,
    crc: u16,
}
