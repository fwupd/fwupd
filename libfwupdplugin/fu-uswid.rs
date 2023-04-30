// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct Uswid {
    magic: guid: const=0x53424F4DD6BA2EACA3E67A52AAEE3BAF,
    hdrver: u8,
    hdrsz: u16le: default=$struct_size,
    payloadsz: u32le,
    flags: u8,
}
