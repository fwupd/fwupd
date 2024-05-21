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
