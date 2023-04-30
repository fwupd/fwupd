// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct Ds20 {
    _reserved: u8,
    guid: guid,
    platform_ver: u32le,
    total_length: u16le,
    vendor_code: u8,
    alt_code: u8,
}
#[derive(New, Validate, Parse)]
struct MsDs20 {
    size: u16le,
    type: u16le,
}
