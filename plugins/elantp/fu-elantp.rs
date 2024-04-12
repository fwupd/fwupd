// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream)]
struct FuStructElantpFirmwareHdr {
    magic: [u8; 6] == 0xAA55CC33FFFF,
}

#[derive(ValidateStream)]
struct FuStructElantpHapticFirmwareHdr {
    magic: [u8; 4] == 0xFF40A25B,
}
