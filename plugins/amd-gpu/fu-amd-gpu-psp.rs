// Copyright (C) 2023 Advanced Micro Devices Inc.
// SPDX-License-Identifier: LGPL-2.1+ OR MIT

#[derive(ParseStream)]
struct Efs {
    signature: u32le = 0x55aa55aa,
    reserved: [u32le; 4],
    psp_dir_loc: u32le,
    reserved: [u32le; 5],
    _psp_dir_loc_back: u32le,
    reserved: [u32le; 6],
    _psp_dir_ind_loc: u32le,
    _rom_strap_a_loc: u32le,
    _rom_strap_b_loc: u32le,
}

#[derive(ValidateStream, Getters)]
struct PspDir {
    cookie: [char; 4] == "$PSP",
    checksum: u32le,
    total_entries: u32le,
    reserved: u32le,
}

#[derive(ParseStream)]
struct PspDirTable {
    fw_id: u32le,
    size: u32le,
    loc: u64le,
}

#[derive(ParseStream)]
struct ImageSlotHeader {
    checksum: u32le,
    boot_priority: u32le,
    update_retries: u32le,
    glitch_retries: u8,
    fw_id: u16le,
    reserved: u8,
    loc: u32le,
    psp_id: u32le,
    slot_max_size: u32le,
    loc_csm: u32le,
}

#[repr(u8)]
enum Fwid {
    AtomCsm = 0x1,
    PartitionAL2 = 0x014D,
    PartitionBL2 = 0x014E,
    IshA = 0x013C,
    IshB = 0x013D,
}
