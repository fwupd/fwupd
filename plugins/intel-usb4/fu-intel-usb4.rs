// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

// hub operation
#[repr(u16le)]
enum IntelUsb4Opcode {
    NVM_WRITE       = 0x20,
    NVM_AUTH_WRITE  = 0x21,
    NVM_READ        = 0x22,
    NVM_SET_OFFSET  = 0x23,
    DROM_READ       = 0x24,
}

#[derive(New, Parse)]
struct IntelUsb4Mbox {
    opcode: IntelUsb4Opcode,
    _rsvd: u8,
    status: u8,
}
