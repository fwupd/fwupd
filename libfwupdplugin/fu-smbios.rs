// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
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

#[derive(New, Parse, ParseBytes)]
#[repr(C, packed)]
struct FuStructSmbiosStructure {
    type: u8,
    length: u8,
    handle: u16le,
}

#[repr(u64le)]
enum FuSmbiosBiosCharacteristics {
    _None = 1 << 0,
    _Reserved = 1 << 1,
    _Unknown = 1 << 2,
    BiosCharacteristicsNotSupported = 1 << 3,
    IsaSupported = 1 << 4,
    McaSupported = 1 << 5,
    EisaSupported = 1 << 6,
    PciSupported = 1 << 7,
    PccardSupported = 1 << 8,
    PlugAndPlaySupported = 1 << 9,
    ApmSupported = 1 << 10,
    BiosIsUpgradeable = 1 << 11,
    BiosShadowingAllowed = 1 << 12,
    VlvesaSupported = 1 << 13,
    EscdSupportAvailable = 1 << 14,
    BootFromCdSupported = 1 << 15,
    SelectableBootSupported = 1 << 16,
}

#[repr(u16le)]
enum FuSmbiosBiosCharacteristicsExt {
    AcpiSupported = 1 << 0,
    UsbLegacySupported = 1 << 1,
    AgpSupported = 1 << 2,
    I2oBootSupported = 1 << 3,
    Ls120SuperDiskBootSupported = 1 << 4,
    AtapiZipSupported = 1 << 5,
    1394BootSupported = 1 << 6,
    SmartBatterySupported = 1 << 7,
    BiosBootSpecificationSupported = 1 << 8,
    FunctionKeyNetworkBootSupported = 1 << 9,
    EnableTargetedContentDistribution = 1 << 10,
    UefiSpecificationSupported = 1 << 11,
    IsVirtualMachine = 1 << 12,
    ManufacturingModeSupported = 1 << 13,
    ManufacturingModeEnabled = 1 << 14,
    _Reserved = 1 << 15,
}

#[derive(ParseBytes, Default)]
#[repr(C, packed)]
struct FuStructSmbiosBiosInformation {
    type: FuSmbiosStructureType == Bios,
    length: u8,
    handle: u16le,
    vendor: u8,
    version: u8,
    starting_addr_segment: u16le,
    release_date: u8,
    rom_size: u8,
    characteristics: FuSmbiosBiosCharacteristics,
    characteristics_ext: FuSmbiosBiosCharacteristicsExt,
}
