// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
#[repr(u8)]
enum WistronDockStatusCode {
    Enter = 0x1,
    Prepare = 0x2,
    Updating = 0x3,
    Complete = 0x4, // unplug cable to trigger update
}

#[derive(Parse)]
struct WistronDockWdit {
    hid_id: u8,
    tag_id: u16be,
    vid: u16le,
    pid: u16le,
    imgmode: u8,
    update_state: u8,
    status_code: WistronDockStatusCode,
    composite_version: u32be,
    device_cnt: u8,
    reserved: u8,
}

#[derive(ToString)]
#[repr(u8)]
enum WistronDockComponentIdx {
    Mcu   = 0x0,
    Pd    = 0x1,
    Audio = 0x2,
    Usb   = 0x3,
    Mst   = 0x4,
    Spi   = 0xA,
    Dock  = 0xF,
}

#[derive(Parse)]
struct WistronDockWditImg {
    comp_id: WistronDockComponentIdx,
    mode: u8,   // 0=single, 1=dual-s, 2=dual-a
    status: u8, // 0=unknown, 1=valid, 2=invalid
    reserved: u8,
    version_build: u32be,
    version1: u32be,
    version2: u32be,
    name: [char; 32],
}
#[derive(ToString)]
enum WistronDockUpdatePhase {
    Download = 0x1,
    Deploy = 0x2,
}
