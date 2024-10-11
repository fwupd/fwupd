// Copyright (C) 2023 Advanced Micro Devices Inc.
// SPDX-License-Identifier: LGPL-2.1+ OR MIT

#[derive(Getters)]
struct VbiosDate {
    month: [char; 2],
    _separator: u8,
    day: [char; 2],
    _separator: u8,
    year: [char; 2],
    _separator: u8,
    hours: [char; 2],
    _separator: u8,
    minutes: [char; 2],
    _separator: u8,
    seconds: [char; 2],
    _nullchar: u8,
}

#[derive(ParseBytes)]
struct AtomImage {
    signature: u16be = 0x55aa,
    size: u16le,
    reserved: [u64be; 2],
    reserved: u32le,
    pcir_loc: u16le,
    reserved: u32le,
    compat_sig: [char; 3] == "IBM",
    checksum: u8,
    reserved: [u32le; 3],
    reserved: u8,
    num_strings: u8,
    reserved: [u64le; 3],
    rom_loc: u16le,
    reserved: [u16le; 3],
    vbios_date: VbiosDate,
    oem: u16le,
    reserved: [u16le; 5],
    str_loc: u32le,
}

#[derive(Getters)]
struct AtomHeaderCommon {
    size: u16le,
    format_rev: u8,
    content_rev: u8,
}

#[derive(ParseBytes, ValidateBytes)]
struct AtomRom21Header {
    header: AtomHeaderCommon,
    signature: [char; 4] == "ATOM" ,
    bios_runtime_seg_addr: u16le,
    protected_mode_info_offset: u16le,
    config_filename_offset: u16le,
    crc_block_offset: u16le,
    bios_bootup_message_offset: u16le,
    int10_offset: u16le,
    pci_bus_dev_init_code: u16le,
    io_base_addr: u16le,
    subsystem_vendor_id: u16le,
    subsystem_id: u16le,
    pci_info_offset: u16le,
    master_command_table_offset: u16le,
    master_data_table_offset: u16le,
    extended_function_code: u8,
    reserved: u8,
    psp_dir_table_offset: u32le,
}

#[repr(u8)]
enum AtomStringIndex {
    PartNumber = 0x00,
    ASIC = 0x01,
    PciType = 0x02,
    MemoryType = 0x03,
}
