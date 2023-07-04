// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct Fmap {
    signature: [char; 8] == "__FMAP__",
    ver_major: u8 = 0x1,
    ver_minor: u8 = 0x1,
    base: u64le,		// address of the firmware binary
    size: u32le,		// bytes
    name: [char; 32],
    nareas: u16le,		// number of areas
}
#[derive(New, Validate, Parse)]
struct FmapArea {		// area of volatile and static regions
    offset: u32le,		// offset relative to base
    size: u32le,		// bytes
    name: [char; 32],	// descriptive name
    flags: u16le,
}
