// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Parse)]
struct Uf2 {
    magic0: u32le: const=0x0A324655,
    magic1: u32le: const=0x9E5D5157,
    flags: u32le,
    target_addr: u32le,
    payload_size: u32le,
    block_no: u32le,
    num_blocks: u32le,
    family_id: u32le,
    data: 476u8,
    magic_end: u32le: const=0x0AB16F30,
}
