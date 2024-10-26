// Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ParseStream, ValidateStream, Default)]
struct FuStructQcFwUpdateHdr {
    magic1: u32be == 0x41505055,
    magic2: u16be == 0x4844,
    magic3: u8 == 0x52,
    protocol: u8,
    length: u32be,
    dev_variant: [u8; 8],
    major: u16be,
    minor: u16be,
    upgrades: u16be,
}
