// Copyright 2026 Arno Dubois <a.dubois@criteo.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// READ BUFFER(10) command descriptor block (SPC-4)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructDellPsbReadBuffer {
    opcode: u8 == 0x3C,
    mode: u8 == 0x01,
    buffer_id: u8,
    offset: u24be,
    length: u24be,
    control: u8,
}

// WRITE BUFFER(10) command descriptor block (SPC-4)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructDellPsbWriteBuffer {
    opcode: u8 == 0x3B,
    mode: u8 == 0x02,
    buffer_id: u8,
    offset: u24be,
    length: u24be,
    control: u8,
}
