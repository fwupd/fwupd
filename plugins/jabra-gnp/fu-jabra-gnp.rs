// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructJabraGnpPacket {
    iface: u8 == 0x05,
    dst: u8,
    _src: u8,
    sequence_number: u8,
    cmd_length: u8 = 0x46,
    cmd: u8,
    sub_cmd: u8,
    //payload: [u8; 57],
}
