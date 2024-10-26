// Copyright 2024 Advanced Micro Devices Inc.
// SPDX-License-Identifier: LGPL-2.1-or-later OR MIT

#[derive(ParseStream, Default)]
struct FuStructAmdKriaPersistReg {
    id_sig: [char; 4] == "ABUM",
    ver: u32le,
    len: u32le,
    checksum: u32le,
    last_booted_img: u8,
    requested_booted_img: u8,
    img_b_bootable: u8,
    img_a_bootable: u8,
    img_a_offset: u32le,
    img_b_offset: u32le,
    recovery_offset: u32le,
}


#[repr(u8)]
enum BootImageId {
    A = 0x00,
    B = 0x01,
}
