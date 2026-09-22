// Copyright 2026 Advanced Micro Devices Inc.
// SPDX-License-Identifier: LGPL-2.1-or-later OR MIT

// PLDM firmware update package, as defined by DMTF DSP0267 1.0.1.
// The fixed-size leading portion of each table is described here; the
// trailing variable-length fields (version strings, descriptors, bitmaps
// and package data) are read by the plugin C code.

// DSP0267 Table 20 -- string type values
#[repr(u8)]
enum FuAmdGpuPldmStringType {
    Unknown = 0x00,
    Ascii = 0x01,
    Utf8 = 0x02,
    Utf16 = 0x03,
    Utf16Le = 0x04,
    Utf16Be = 0x05,
}

// DSP0267 Table 7 -- descriptor identifier table
#[repr(u16le)]
enum FuAmdGpuPldmDescriptorType {
    PciVendorId = 0x0000,
    IanaEnterpriseId = 0x0001,
    Uuid = 0x0002,
    PnpVendorId = 0x0003,
    AcpiVendorId = 0x0004,
    PciDeviceId = 0x0100,
    PciSubsystemVendorId = 0x0101,
    PciSubsystemId = 0x0102,
    PciRevisionId = 0x0103,
    PnpProductId = 0x0104,
    AcpiProductId = 0x0105,
    VendorDefined = 0xFFFF,
}

// DSP0267 Table 3 -- PLDM firmware package header (Package Header Information)
#[derive(New, ParseStream, ValidateStream, Getters, Default)]
#[repr(C, packed)]
struct FuStructAmdGpuPldmHeader {
    // PackageHeaderIdentifier: the UUID is stored big-endian on the wire as
    // F018878CCB7D49439800A02F059ACA02; the value below is the same 16 bytes
    // expressed as a mixed-endian GUID string.
    identifier: Guid == "8c8718f0-7dcb-4349-9800-a02f059aca02",
    format_revision: u8 = 0x01,
    header_size: u16le,
    release_date_time: [u8; 13],
    component_bitmap_bit_length: u16le,
    version_string_type: FuAmdGpuPldmStringType,
    version_string_length: u8,
}

// DSP0267 Table 4 -- Firmware Device ID record
#[derive(New, ParseStream, Getters, Default)]
#[repr(C, packed)]
struct FuStructAmdGpuPldmDeviceIdRecord {
    record_length: u16le,
    descriptor_count: u8,
    device_update_option_flags: u32le,
    version_string_type: FuAmdGpuPldmStringType,
    version_string_length: u8,
    package_data_length: u16le,
}

// DSP0267 Table 6 -- Descriptor definition (Type, Length, Value)
#[derive(New, ParseStream, Getters, Default)]
#[repr(C, packed)]
struct FuStructAmdGpuPldmDescriptor {
    descriptor_type: u16le,
    descriptor_length: u16le,
}

// DSP0267 Table 5 -- Component image information
#[derive(New, ParseStream, Getters, Default)]
#[repr(C, packed)]
struct FuStructAmdGpuPldmComponent {
    classification: u16le,
    identifier: u16le,
    comparison_stamp: u32le,
    options: u16le,
    requested_activation_method: u16le,
    location_offset: u32le,
    size: u32le,
    version_string_type: FuAmdGpuPldmStringType,
    version_string_length: u8,
}
