// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum IfdRegion {
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

#[derive(ParseBytes, New, ValidateBytes)]
struct IfdFdbar {
    reserved: [u8; 16] = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF,
    signature: u32le == 0x0FF0A55A,
    descriptor_map0: u32le,
    descriptor_map1: u32le,
    descriptor_map2: u32le,
}

#[derive(ParseBytes, New)]
struct IfdFcba {
    flcomp: u32le,
    flill: u32le,
    flill1: u32le,
}
