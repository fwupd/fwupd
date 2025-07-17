// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
#[repr(u8)]
enum FuWistronDockStatusCode {
    Enter = 0x1,
    Prepare = 0x2,
    Updating = 0x3,
    Complete = 0x4, // unplug cable to trigger update
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructWistronDockWdit {
    hid_id: u8,
    tag_id: u16be,
    vid: u16le,
    pid: u16le,
    imgmode: u8,
    update_state: u8,
    status_code: FuWistronDockStatusCode,
    composite_version: u32be,
    device_cnt: u8,
    reserved: u8,
}

#[derive(ToString)]
#[repr(u8)]
enum FuWistronDockComponentIdx {
    Mcu   = 0x0,
    Pd    = 0x1,
    Audio = 0x2,
    Usb   = 0x3,
    Mst   = 0x4,
    Spi   = 0xA,
    Dock  = 0xF,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructWistronDockWditImg {
    comp_id: FuWistronDockComponentIdx,
    mode: u8,   // 0=single, 1=dual-s, 2=dual-a
    status: u8, // 0=unknown, 1=valid, 2=invalid
    reserved: u8,
    version_build: u32be,
    version1: u32be,
    version2: u32be,
    name: [char; 32],
}

#[derive(ToString)]
enum FuWistronDockUpdatePhase {
    Download = 0x1,
    Deploy = 0x2,
}

enum FuWistronDockCmdIcp {
    Enter = 0x81,
    Exit = 0x82,
    Address = 0x84,
    Readblock = 0x85,
    Writeblock = 0x86,
    Mcuid = 0x87,
    Bbinfo = 0x88, // bb code information
    Userinfo = 0x89, // user code information
    Done = 0x5A,
    Error = 0xFF,
    ExitWdreset = 0x01, // exit ICP with watch dog reset
}

enum FuWistronDockCmdDfu {
    Enter = 0x91,
    Exit = 0x92,
    Address = 0x93,
    ReadimgBlock = 0x94,
    WriteimgBlock = 0x95,
    Verify = 0x96,
    CompositeVer = 0x97,
    WriteWdflSig = 0x98,
    WriteWdflData = 0x99,
    VerifyWdfl = 0x9A,
    SerinalNumber = 0x9B,
    Done = 0x5A,
    Error = 0xFF,
}
