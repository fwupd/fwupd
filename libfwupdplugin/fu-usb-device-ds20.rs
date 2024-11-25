// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, ParseBytes)]
struct FuStructDs20 {
    _reserved: u8,
    guid: Guid,
    platform_ver: u32le,
    total_length: u16le,
    vendor_code: u8,
    alt_code: u8,
}
#[derive(New, ParseBytes)]
struct FuStructMsDs20 {
    size: u16le,
    type: u16le,
}
