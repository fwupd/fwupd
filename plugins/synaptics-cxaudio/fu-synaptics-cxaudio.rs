// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(Parse)]
struct SynapticsCxaudioCustomInfo {
    patch_version_string_address: u16le,
    cpx_patch_version: [u8; 3],
    spx_patch_version: [u8; 4],
    layout_signature: u8,
    layout_version: u8,
    application_status: u8,
    vendor_id: u16le,
    product_id: u16le,
    revision_id: u16le,
    language_string_address: u16le,
    manufacturer_string_address: u16le,
    product_string_address: u16le,
    serial_number_string_address: u16le,
}
#[derive(Parse)]
struct SynapticsCxaudioStringHeader {
    length: u8,
    type: u8 == 0x03,
}
#[derive(Parse)]
struct SynapticsCxaudioValiditySignature {
    magic_byte: u8 = 0x4C,    // 'L'
    eeprom_size_code: u8,
}
#[derive(Parse, Setters)]
struct SynapticsCxaudioPatchInfo {
    patch_signature: u8,
    patch_address: u16le,
}
