// Copyright (C) {{Year}} {{Author}} <{{Email}}>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, ValidateBytes, Parse)]
struct FuStruct{{Vendor}}{{Example}} {
    signature: u8 == 0xDE,
    address: u16le,
}

#[derive(ToString)]
enum Fu{{Vendor}}{{Example}}Status {
    Unknown,
    Failed,
}
