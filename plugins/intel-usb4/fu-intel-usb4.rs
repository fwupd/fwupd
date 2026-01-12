// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// hub operation
#[repr(u16le)]
enum FuIntelUsb4Opcode {
    NvmWrite        = 0x20,
    NvmAuthWrite    = 0x21,
    NvmRead         = 0x22,
    NvmSetOffset    = 0x23,
    DromRead        = 0x24,
}

#[derive(New, Parse)]
#[repr(C, packed)]
struct FuStructIntelUsb4Mbox {
    opcode: FuIntelUsb4Opcode,
    _rsvd: u8,
    status: u8,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructIntelUsb4MetadataNvmRead {
    address: u24le,
    length: u8, // in DWORDs
}
