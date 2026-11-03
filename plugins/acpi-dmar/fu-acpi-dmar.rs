// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuDmarFlags {
    None = 0,
    IntrRemap = 1 << 0,
    X2apicOptOut = 1 << 1,
    DmaCtrlPlatformOptInFlag = 1 << 2,
}

#[derive(New, ParseStream, ToBytes)]
#[repr(C, packed)]
struct FuStructAcpiDmar {
    host_address_width: u8,
    flags: FuDmarFlags,
    reserved: [u8; 10],
}
