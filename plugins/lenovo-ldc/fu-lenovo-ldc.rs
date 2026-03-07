// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructLenovoLdcHdr {
    signature: u8 == 0xDE,
    address: u16le,
}

#[derive(ToString)]
enum FuLenovoLdcStatus {
    Unknown,
    Failed,
}
