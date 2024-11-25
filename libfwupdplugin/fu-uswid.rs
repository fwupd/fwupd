// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

enum FuUswidHeaderFlag {
    None = 0b0,
    Compressed = 0b1,
}

#[derive(ToString, FromString)]
enum FuUswidPayloadCompression {
    None = 0x00,
    Zlib = 0x01,
    Lzma = 0x02,
}

#[derive(New, ValidateBytes, ParseBytes)]
struct FuStructUswid {
    magic: Guid == 0x53424F4DD6BA2EACA3E67A52AAEE3BAF,
    hdrver: u8,
    hdrsz: u16le = $struct_size,
    payloadsz: u32le,
    flags: u8,
    compression: u8,
}
