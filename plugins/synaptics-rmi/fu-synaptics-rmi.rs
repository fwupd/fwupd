// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
#[repr(u16le)]
enum RmiPartitionId {
    None = 0x00,
    Bootloader = 0x01,
    DeviceConfig,
    FlashConfig,
    ManufacturingBlock,
    GuestSerialization,
    GlobalParameters,
    CoreCode,
    CoreConfig,
    GuestCode,
    DisplayConfig,
    ExternalTouchAfeConfig,
    UtilityParameter,
    Pubkey,
    FixedLocationData = 0x0E,
}

#[derive(Parse)]
struct RmiPartitionTbl {
    partition_id: RmiPartitionId,
    partition_len: u16le,
    partition_addr: u16le,
    partition_prop: u16le,
}
#[derive(New, ParseStream)]
struct RmiImg {
    checksum: u32le,
    _reserved1: [u8; 2],
    io_offset: u8,
    bootloader_version: u8,
    image_size: u32le,
    config_size: u32le,
    product_id: [char; 10],
    package_id: u32le,
    product_info: u32le,
    _reserved3: [u8; 46],
    fw_build_id: u32le,
    signature_size: u32le,
}

#[derive(ToString)]
#[repr(u16le)]
enum RmiContainerId {
    TopLevel,
    Ui,
    UiConfig,
    Bl,
    BlImage,
    BlConfig,
    BlLockdownInfo,
    PermanentConfig,
    GuestCode,
    BlProtocolDescriptor,
    UiProtocolDescriptor,
    RmiSelfDiscovery,
    RmiPageContent,
    GeneralInformation,
    DeviceConfig,
    FlashConfig,
    GuestSerialization,
    GlobalParameters,
    CoreCode,
    CoreConfig,
    DisplayConfig,
    ExternalTouchAfeConfig,
    Utility,
    UtilityParameter,
    FixedLocationData = 27,
}

#[derive(New, ParseStream)]
struct RmiContainerDescriptor {
    content_checksum: u32le,
    container_id: RmiContainerId,
    minor_version: u8,
    major_version: u8,
    signature_size: u32le,
    container_option_flags: u32le,
    content_options_length: u32le,
    content_options_address: u32le,
    content_length: u32le,
    content_address: u32le,
}
