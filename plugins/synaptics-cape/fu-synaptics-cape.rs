// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Parse)]
struct SynapticsCapeFileHdr {
    vid: u32le,
    pid: u32le,
    update_type: u32le,
    signature: u32le,
    crc: u32le,
    ver_w: u16le,
    ver_x: u16le,
    ver_y: u16le,
    ver_z: u16le,
    reserved: u32le,
}

