// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum SynapticsCxaudioDeviceKind {
    Unknown,
    Cx20562 = 20562,
    Cx2070x = 20700,
    Cx2077x = 20770,
    Cx2076x = 20760,
    Cx2085x = 20850,
    Cx2089x = 20890,
    Cx2098x = 20980,
    Cx2198x = 21980,
}

enum SynapticsCxaudioMemKind {
    Eeprom,
    CpxRam,
    CpxRom,
}

#[derive(ToString)]
enum SynapticsCxaudioFileKind {
    Unknown,
    Cx2070xFw,
    Cx2070xPatch,
    Cx2077xPatch,
    Cx2076xPatch,
    Cx2085xPatch,
    Cx2089xPatch,
    Cx2098xPatch,
    Cx2198xPatch,
}

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
