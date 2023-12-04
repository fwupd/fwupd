// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ValidateStream)]
struct ElantpFirmwareHdr {
    magic: [u8; 6] == 0xAA55CC33FFFF,
}

#[derive(ValidateStream)]
struct ElantpHapticFirmwareHdr {
    magic: [u8; 4] == 0xFF40A25B,
}
