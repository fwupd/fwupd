// Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ValidateBytes, Parse)]
struct FuStructWeidaRaw {
    signature: u8 == 0xDE,
    address: u16le,
}

#[derive(ToString)]
enum FuWeidaRawStatus {
    Unknown,
    Failed,
}
