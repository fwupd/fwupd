// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuUsbDirection {
    DeviceToHost, // IN
    HostToDevice, // OUT
}

enum FuUsbRequestType {
    Standard,
    Class,
    Vendor,
    Reserved,
}

enum FuUsbRecipient {
    Device,
    Interface,
    Endpoint,
    Other,
}

#[derive(ToString)]
#[repr(u8)]
enum FuUsbClass {
    InterfaceDesc = 0x00,
    Audio = 0x01,
    Communications = 0x02,
    Hid = 0x03,
    Physical = 0x05,
    Image = 0x06,
    Printer = 0x07,
    MassStorage = 0x08,
    Hub = 0x09,
    Cdc_data = 0x0A,
    SmartCard = 0x0B,
    ContentSecurity = 0x0D,
    Video = 0x0E,
    PersonalHealthcare = 0x0F,
    AudioVideo = 0x10,
    Billboard = 0x11,
    Diagnostic = 0xDC,
    WirelessController = 0xE0,
    Miscellaneous = 0xEF,
    ApplicationSpecific = 0xFE,
    VendorSpecific = 0xFF,
}

enum FuUsbLangid {
    Invalid = 0x0000,
    EnglishUnitedStates = 0x0409,
}

#[derive(ToString)]
#[repr(u8)]
enum FuUsbDescriptorKind {
    Device = 0x01,
    Config = 0x02,
    String = 0x03,
    Interface = 0x04,
    Endpoint = 0x05,
    InterfaceAssociation = 0x0B,
    Bos = 0x0F,
    DeviceCapability = 0x10,
    Hid = 0x21,
    Report = 0x22,
    Physical = 0x23,
    Hub = 0x29,
    SuperspeedHub = 0x2A,
    SsEndpointCompanion = 0x30,
}

#[derive(ParseStream, Parse)]
struct FuUsbBaseHdr {
    length: u8,
    descriptor_type: FuUsbDescriptorKind,
}

#[derive(ParseStream, Default)]
struct FuUsbDeviceHdr {
    length: u8,
    descriptor_type: FuUsbDescriptorKind == Device,
    usb: u16le,
    device_class: FuUsbClass,
    device_sub_class: u8,
    device_protocol: u8,
    max_packet_size0: u8,
    vendor: u16le,
    product: u16le,
    device: u16le,
    manufacturer_idx: u8,
    product_idx: u8,
    serial_number_idx: u8,
    num_configurations: u8,
};

#[derive(ParseStream, Default)]
struct FuUsbDescriptorHdr {
    length: u8,
    descriptor_type: FuUsbDescriptorKind == Config,
    total_length: u16le,
    num_interfaces: u8,
    configuration_value: u8,
    configuration: u8,
    attributes: u8,
    max_power: u8,
}

#[derive(ParseStream, Default)]
struct FuUsbHidDescriptorHdr {
    length: u8,
    descriptor_type: FuUsbDescriptorKind == Hid,
    hid: u16le,
    country_code: u8,
    num_descriptors: u8,
    class_descriptor_type: u8,
    class_descriptor_length: u16le,
}

#[derive(ParseBytes, Default)]
struct FuUsbDfuDescriptorHdr {
    length: u8,
    descriptor_type: FuUsbDescriptorKind == Hid,
    attributes: u8,
    detach_timeout: u16le,
    transfer_size: u16le,
    dfu_version: u16le,
}

#[derive(ParseStream, Default)]
struct FuUsbInterfaceHdr {
    length: u8,
    descriptor_type: FuUsbDescriptorKind == Interface,
    interface_number: u8,
    alternate_setting: u8,
    num_endpoints: u8,
    interface_class: FuUsbClass,
    interface_sub_class: u8,
    interface_protocol: u8,
    interface: u8,
}

#[derive(ParseStream)]
struct FuUsbEndpointHdr {
    length: u8,
    descriptor_type: FuUsbDescriptorKind,
    endpoint_address: u8,
    attributes: u8,
    max_packet_size: u16le,
    interval: u8,
}

#[derive(ParseStream)]
struct FuUsbBosHdr {
    length: u8,
    descriptor_type: FuUsbDescriptorKind,
    dev_capability_type: u8,
}
