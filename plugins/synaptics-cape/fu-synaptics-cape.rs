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

struct SynapticsCapeSnglHdr {
    magic: u32le,
    file_crc: u32le,
    file_size: u32le,
    magic2: u32le,
    img_type: u32le,
    fw_version: u32le,
    vid: u16le,
    pid: u16le,
    fw_file_num: u32le,
    version: u32le,
    fw_crc: u32le,
    _reserved: [u8; 8],
    machine_name: [char; 16],
    time_stamp: [char; 16],
}

enum SynapticsCapeSnglImgTypeId {
    Hid0 = 0x30444948, // hid file for partition 0
    Hid1 = 0x31444948, // hid file for partition 1
    Hof0 = 0x30464F48, // hid + offer file for partition 0
    Hof1 = 0x31464F48, // hid + offer file for partition 1
    Sfsx = 0x58534653, // sfs file
    Sofx = 0x58464F53, // sfs + offer file
    Sign = 0x4e474953, // digital signature file
}

enum SynapticsCapeFirmwarePartition {
    Auto,
    1,
    2,
}
