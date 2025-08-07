/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#[repr(u8)]
enum FuDellKestrelHidCmd {
    WriteData = 0x40,
    ExtI2cWrite = 0xc6,
    ExtI2cRead = 0xd6,
}

#[repr(C, packed)]
#[derive(New, Setters, Getters, Default)]
struct FuStructDellKestrelHidCmdBuffer {
    cmd: u8,
    ext: u8,
    dwregaddr: u32le,
    bufferlen: u16le,
    parameters: [u8; 3] = 0xEC0180, // addr, length, speed
    extended_cmdarea: [u8; 53],
    databytes: [u8; 192],
}

#[derive(New, Setters)]
struct FuStructDellKestrelHidFwUpdatePkg {
    cmd: u8,
    ext: u8,
    chunk_sz: u32be,
    sub_cmd: u8,
    dev_type: u8,
    dev_identifier: u8,
    fw_sz: u32be,
}

#[repr(u8)]
#[derive(ToString)]
enum FuDellKestrelHidEcChunkResponse {
    Unknown = 0,
    UpdateComplete,
    SendNextChunk,
    UpdateFailed,
}
