// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

use fwupd::intel_me::FuStructIntelMeHfsts;
use fwupd::smbios::FuStructSmbiosStructure;

enum FuSmbiosDdHandle {
    Fsp = 0x16,
    Ucode = 0x17,
    Me = 0x18,
    Pch = 0x19,
    Me2 = 0x30,
}

//#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructMeVersion {
    major: u8,
    minor: u8,
    patch: u8,
    build: u16le,
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructMeFviHeader { // Firmware Version Info
    count: u8,
    // data: [FuStructMeFviData; $count],
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructMeFviData { // Firmware Version Info
    component_name: u8,
    component_string: u8,
    version: FuStructMeVersion,
}

#[derive(ParseBytes, Default)]
#[repr(C, packed)]
struct FuStructSmbiosFwsts {
    type: u8 == 0xDB,
    length: u8,
    handle: u16le,
    version: u8 == 0x01,
    count: u8,
    // items: [FuStructSmbiosFwstsRecord; count],
}

#[repr(u8)]
enum FuSmbiosFwstsComponentName {
    Mei1 = 1, // 0:22:0
    Mei2 = 2, // 0:22:1
    Mei3 = 3, // 0:22:4
    Mei4 = 4, // ?
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructSmbiosFwstsRecord {
    component_name: FuSmbiosFwstsComponentName,
    fwsts: [FuStructIntelMeHfsts; 6],
}
