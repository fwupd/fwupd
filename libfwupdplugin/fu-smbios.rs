// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuSmbiosStructureType {
    Bios,
    System,
    Baseboard, // aka motherboard
    Chassis,
}

#[derive(ToString)]
enum FuSmbiosChassisKind {
    Unset, // inferred
    Other,
    Unknown,
    Desktop,
    LowProfileDesktop,
    PizzaBox,
    MiniTower,
    Tower,
    Portable,
    Laptop,
    Notebook,
    HandHeld,
    DockingStation,
    AllInOne,
    SubNotebook,
    SpaceSaving,
    LunchBox,
    MainServer,
    Expansion,
    Subchassis,
    BusExpansion,
    Peripheral,
    Raid,
    RackMount,
    SealedCasePc,
    MultiSystem,
    CompactPci,
    AdvancedTca,
    Blade,
    Reserved, // 0x1D is missing!
    Tablet,
    Convertible,
    Detachable,
    IotGateway,
    EmbeddedPc,
    MiniPc,
    StickPc,
}

#[derive(New, Parse)]
#[repr(C, packed)]
struct FuStructSmbiosEp32 {
    anchor_str: [char; 4],
    entry_point_csum: u8,
    entry_point_len: u8,
    smbios_major_ver: u8,
    smbios_minor_ver: u8,
    max_structure_sz: u16le,
    entry_point_rev: u8,
    _formatted_area: [u8; 5],
    intermediate_anchor_str: [char; 5],
    intermediate_csum: u8,
    structure_table_len: u16le,
    structure_table_addr: u32le,
    number_smbios_structs: u16le,
    smbios_bcd_rev: u8,
}

#[derive(New, Parse)]
#[repr(C, packed)]
struct FuStructSmbiosEp64 {
    anchor_str: [char; 5],
    entry_point_csum: u8,
    entry_point_len: u8,
    smbios_major_ver: u8,
    smbios_minor_ver: u8,
    smbios_docrev: u8,
    entry_point_rev: u8,
    reserved0: u8,
    structure_table_len: u32le,
    structure_table_addr: u64le,
}

#[derive(New, Parse)]
#[repr(C, packed)]
struct FuStructSmbiosStructure {
    type: u8,
    length: u8,
    handle: u16le,
}
