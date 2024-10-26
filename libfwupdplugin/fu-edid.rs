// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuEdidDescriptorTag {
    DisplayProductSerialNumber = 0xFF,
    AlphanumericDataString = 0xFE,
    DisplayRangeLimits = 0xFD,
    DisplayProductName = 0xFC,
    ColorPointData = 0xFB,
    StandardTimingIdentifications = 0xFA,
    DisplayColorManagementData = 0xF9,
    CvtTimingCodes = 0xF8,
    EstablishedTimings = 0xF7,
    DummyDescriptor = 0x10,
}

#[derive(ParseStream, New)]
struct FuStructEdidDescriptor {
    kind: u16le,
    subkind: u8,
    tag: FuEdidDescriptorTag,
    _reserved: u8,
    data: [u8; 13],
}

#[derive(New, ParseStream, Default)]
struct FuStructEdid {
    header: [u8; 8] == 0x00FFFFFFFFFFFF00,
    manufacturer_name: [u8; 2],
    product_code: u16le,
    serial_number: u32le,
    week_of_manufacture: u8,
    year_of_manufacture: u8,
    edid_version_number: u8 == 0x1,
    revision_number: u8 = 0x3,
    _basic_display_parameters_and_features: [u8; 5],
    _color_characteristics: [u8; 10],
    _established_timings: [u8; 3],
    _standard_timings: [u8; 16],
    data_blocks: [u8; 72], // should be [FuEdidDescriptor: 4],
    extension_block_count: u8,
    checksum: u8,
}
