// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// hub operation
#[repr(u16le)]
enum FuIntelUsb4Opcode {
    NVM_WRITE       = 0x20,
    NVM_AUTH_WRITE  = 0x21,
    NVM_READ        = 0x22,
    NVM_SET_OFFSET  = 0x23,
    DROM_READ       = 0x24,
}

#[derive(New, Parse)]
struct FuStructIntelUsb4Mbox {
    opcode: FuIntelUsb4Opcode,
    _rsvd: u8,
    status: u8,
}

#[derive(New)]
struct FuStructIntelUsb4MetadataNvmRead {
    address: u24le,
    length: u8, // in DWORDs
}
