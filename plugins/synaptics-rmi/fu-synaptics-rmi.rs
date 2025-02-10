// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
#[repr(u16le)]
enum FuRmiPartitionId {
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
#[repr(C, packed)]
struct FuStructRmiPartitionTbl {
    partition_id: FuRmiPartitionId,
    partition_len: u16le,
    partition_addr: u16le,
    partition_prop: u16le,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructRmiImg {
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
enum FuRmiContainerId {
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
#[repr(C, packed)]
struct FuStructRmiContainerDescriptor {
    content_checksum: u32le,
    container_id: FuRmiContainerId,
    minor_version: u8,
    major_version: u8,
    signature_size: u32le,
    container_option_flags: u32le,
    content_options_length: u32le,
    content_options_address: u32le,
    content_length: u32le,
    content_address: u32le,
}

// PS2 Data Port Command
enum FuRmiEdpCommand {
    AuxFullRmiBackDoor = 0x7F,
    AuxAccessModeByte1 = 0xE0,
    AuxAccessModeByte2 = 0xE1,
    AuxIbmReadSecondaryId = 0xE1,
    AuxSetScaling1To1 = 0xE6,
    AuxSetScaling2To1 = 0xE7,
    AuxSetResolution = 0xE8,
    AuxStatusRequest = 0xE9,
    AuxSetStreamMode = 0xEA,
    AuxReadData = 0xEB,
    AuxResetWrapMode = 0xEC,
    AuxSetWrapMode = 0xEE,
    AuxSetRemoteMode = 0xF0,
    AuxReadDeviceType = 0xF2,
    AuxSetSampleRate = 0xF3,
    AuxEnable = 0xF4,
    AuxDisable = 0xF5,
    AuxSetDefault = 0xF6,
    AuxResend = 0xFE,
    AuxReset = 0xFF,
}

enum FuRmiDeviceResponse {
    TouchPad = 0x47,
    Styk = 0x46,
    ControlBar = 0x44,
    RgbControlBar = 0x43,
}

enum FuRmiStatusRequest {
    IdentifySynaptics = 0x00,
    ReadTouchPadModes = 0x01,
    ReadModeByte = 0x01,
    ReadEdgeMargins = 0x02,
    ReadCapabilities = 0x02,
    ReadModelID = 0x03,
    ReadCompilationDate = 0x04,
    ReadSerialNumberPrefix = 0x06,
    ReadSerialNumberSuffix = 0x07,
    ReadResolutions = 0x08,
    ReadExtraCapabilities1 = 0x09,
    ReadExtraCapabilities2 = 0x0A,
    ReadExtraCapabilities3 = 0x0B,
    ReadExtraCapabilities4 = 0x0C,
    ReadExtraCapabilities5 = 0x0D,
    ReadCoordinates = 0x0D,
    ReadExtraCapabilities6 = 0x0E,
    ReadExtraCapabilities7 = 0x0F,
}

enum FuRmiDataPortStatus {
    Acknowledge = 0xFA,
    Error = 0xFC,
    Resend = 0xFE,
    TimeOut = 0x100,
}

enum FuRmiSetSampleRate {
    SetModeByte1 = 0x0A,
    SetModeByte2 = 0x14,
    SetModeByte3 = 0x28,
    SetModeByte4 = 0x3C,
    SetDeluxeModeByte1 = 0x0A,
    SetDeluxeModeByte2 = 0x3C,
    SetDeluxeModeByte3 = 0xC8,
    FastRecalibrate = 0x50,
    PassThroughCommandTunnel = 0x28,
}

enum FuRmiStickDeviceType {
    None = 0,
    Ibm,
    JytSyna = 5,
    Synaptics = 6,
    Unknown = 0xFFFFFFFF,
}
