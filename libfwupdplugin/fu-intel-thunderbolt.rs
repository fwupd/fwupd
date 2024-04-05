// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuIntelThunderboltNvmSection {
    Digital,
    Drom,
    ArcParams,
    DramUcode,
    Last,
}

#[repr(u8)]
enum FuIntelThunderboltNvmSectionFlag {
    Dram = 1 << 6,
}

#[derive(ToString, FromString)]
enum FuIntelThunderboltNvmFamily {
    Unknown,
    FalconRidge,
    WinRidge,
    AlpineRidge,
    AlpineRidgeC,
    TitanRidge,
    Bb,
    MapleRidge,
    GoshenRidge,
}

#[derive(New)]
struct FuIntelThunderboltNvmDigital {
    reserved: [u8; 2],
    available_sections: u8, // FuIntelThunderboltNvmSectionFlag
    ucode: u16, // addr
    device_id: u32,
    version: u16,
    reserved: [u8; 5],
    flags_host: u8,
    reserved: [u8; 52],
    flash_size: u8,
    reserved: [u8; 47],
    arc_params: u32,
    reserved: [u8; 2],
    flags_is_native: u8,
    reserved: [u8; 146],
    drom: u32,
    reserved: [u8; 14],
}

#[derive(New)]
struct FuIntelThunderboltNvmDrom {
    reserved: [u8; 16],
    vendor_id: u16,
    model_id: u16,
    reserved: [u8; 12],
}

#[derive(New)]
struct FuIntelThunderboltNvmArcParams {
    reserved: [u8; 268],
    pd_pointer: u32,
    reserved: [u8; 16],
}

#[derive(New)]
struct FuIntelThunderboltNvmDram {
    reserved: [u8; 16],
}
