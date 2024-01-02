// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ValidateStream)]
struct AcpiPhatHdr {
    magic: [char; 4] == "PHAT",
}

#[derive(New, ParseStream)]
struct AcpiPhatHealthRecord {
    signature: u16le = 0x1,
    rcdlen: u16le,
    version: u8,
    reserved: [u8; 2],
    flags: u8,
    device_signature: Guid,
    device_specific_data: u32le,
}
#[derive(New, ParseStream)]
struct AcpiPhatVersionElement {
    component_id: Guid,
    version_value: u64le,
    producer_id: [char; 4],
}
#[derive(New, ParseStream)]
struct AcpiPhatVersionRecord {
    signature: u16le = 0x0,
    rcdlen: u16le,
    version: u8,
    reserved: [u8; 3],
    record_count: u32le,
}
