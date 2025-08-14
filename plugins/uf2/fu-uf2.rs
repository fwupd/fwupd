// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u32le)]
enum FuUf2FirmwareBlockFlags {
    None = 0x00000000,
    Noflash = 0x00000001,
    IsContainer = 0x00001000,
    HasFamily = 0x00002000,
    HasMd5 = 0x00004000,
    HasExtensionTag = 0x00008000,
}

#[repr(u24le)]
#[derive(ToString)]
enum FuUf2FirmwareTag {
    Version = 0x9FC7BC,     // semver of firmware file (UTF-8)
    Description = 0x650D9D, // description of device (UTF-8)
    PageSz = 0x0BE9F7,      // page size of target device (uint32_t)
    Sha2 = 0xB46DB0,        // checksum of firmware
    DeviceId = 0xC8A729,    // device type identifier (uint32_t or uint64_t)
}

#[derive(New, Parse)]
#[repr(C, packed)]
struct FuStructUf2Extension {
    size: u8,
    tag: FuUf2FirmwareTag,
}

#[derive(New, Parse, Default)]
#[repr(C, packed)]
struct FuStructUf2 {
    magic0: u32le == 0x0A324655,
    magic1: u32le == 0x9E5D5157,
    flags: FuUf2FirmwareBlockFlags,
    target_addr: u32le,
    payload_size: u32le,
    block_no: u32le,
    num_blocks: u32le,
    family_id: u32le,
    data: [u8; 476],
    magic_end: u32le == 0x0AB16F30,
}
