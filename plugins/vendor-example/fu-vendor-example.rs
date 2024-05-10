// Copyright {{Year}} {{Author}} <{{Email}}>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ValidateStream, ParseStream)]
struct FuStruct{{Vendor}}{{Example}} {
    signature: u8 == 0xDE,
    address: u16le,
}

#[derive(ToString)]
enum Fu{{Vendor}}{{Example}}Status {
    Unknown,
    Failed,
}
