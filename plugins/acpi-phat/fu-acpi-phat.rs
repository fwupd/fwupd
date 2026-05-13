// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructAcpiPhatHdr {
    magic: [char; 4] == "PHAT",
}

#[derive(New, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructAcpiPhatHealthRecord {
    signature: u16le = 0x1,
    rcdlen: u16le,
    version: u8,
    reserved: [u8; 2],
    flags: u8,
    device_signature: Guid,
    device_specific_data: u32le,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructAcpiPhatVersionElement {
    component_id: Guid,
    version_value: u64le,
    producer_id: [char; 4],
}

#[derive(New, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructAcpiPhatVersionRecord {
    signature: u16le = 0x0,
    rcdlen: u16le,
    version: u8,
    reserved: [u8; 3],
    record_count: u32le,
}
