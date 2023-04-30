// Copyright (C) {{Year}} {{Author}} <{{Email}}>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct {{Vendor}}{{Example}} {
    signature: u8: const=0xDE,
    address: u16le,
}
#[derive(ToString)]
enum {{Vendor}}{{Example}}Status {
    Unknown,
    Failed,
}
