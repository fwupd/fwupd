// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ParseBytes)]
struct FuStructDellSmbiosDa {
    type: u8,
    length: u8,
    handle: u16le,
    cmd_address: u16le,
    cmd_code: u8,
    supported_cmds: u32le,
    // tokens here
}
