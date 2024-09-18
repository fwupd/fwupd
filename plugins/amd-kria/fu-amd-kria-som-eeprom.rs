// Copyright 2024 Advanced Micro Devices Inc.
// SPDX-License-Identifier: LGPL-2.1-or-later OR MIT

// https://www.intel.com/content/dam/www/public/us/en/documents/specification-updates/ipmi-platform-mgt-fru-info-storage-def-v1-0-rev-1-3-spec-update.pdf

#[derive(ParseStream)]
struct FuStructIpmiCommon {
    version: u8 = 0x1,
    internal_offest: u8,
    chassis_offeset: u8,
    board_offset: u8,
    product_offset: u8,
    multirecord_offset: u8,
    reserved: u8,
    checksum: u8,
}

#[derive(ParseStream)]
struct FuStructBoardInfo {
    version: u8 = 0x1,
    length: u8,
    lang_code: u8,
    mfg_date: u24le,
    manufacturer_len: u8,
}

#[repr(u8)]
enum TypeCode {
    Binary = 0,
    BcdPlus = 1,
    Acsii6 = 2,
    LangCodeDep = 3,
}
