// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

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

#[derive(New, ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructUswid {
    magic: Guid == "4d4f4253-bad6-ac2e-a3e6-7a52aaee3baf",
    hdrver: u8,
    hdrsz: u16le = $struct_size,
    payloadsz: u32le,
    flags: u8,
    compression: u8,
}
