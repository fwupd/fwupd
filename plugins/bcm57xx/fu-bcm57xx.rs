// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructBcm57xxNvramHeader {
    magic: u32be,
    phys_addr: u32be,
    size_wrds: u32be,
    offset: u32be,
    crc: u32be,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructBcm57xxNvramDirectory {
    addr: u32be,
    size_wrds: u32be,
    offset: u32be,
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuStructBcm57xxNvramInfo {
    mac_addr: [u32be; 11],
    device: u16be,
    vendor: u16be,
    _reserved: [u8; 92],
}
