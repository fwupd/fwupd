// Copyright 2023 GN Audio
// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuJabraFilePacketCmd {
    Identity = 0x02,
    File = 0x03,
    Dfu = 0x07,
    Video = 0x26,
}

#[derive(New)]
struct FuJabraFilePacket {
    iface: u8 == 0x05,
    dst: u8,
    src: u8,
    sequence_number: u8,
    cmd_length: u8 = 0x46,
    cmd: FuJabraFilePacketCmd,
    sub_cmd: u8,
    payload: [u8; 57],
}
