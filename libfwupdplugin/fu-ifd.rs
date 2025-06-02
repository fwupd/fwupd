// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuIfdRegion {
    Desc = 0x00,
    Bios = 0x01,
    Me = 0x02,
    Gbe = 0x03,
    Platform = 0x04,
    Devexp = 0x05,
    Bios2 = 0x06,
    Ec = 0x08,
    Ie = 0x0A,
    10gbe = 0x0B,
    Max = 0x0F,
}

#[derive(ParseStream, New, ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructIfdFdbar {
    reserved: [u8; 16] = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF,
    signature: u32le == 0x0FF0A55A,
    descriptor_map0: u32le,
    descriptor_map1: u32le,
    descriptor_map2: u32le,
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuStructIfdFcba {
    flcomp: u32le,
    flill: u32le,
    flill1: u32le,
}

#[derive(ToBitString)]
#[repr(C, packed)]
enum FuIfdAccess {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
}
