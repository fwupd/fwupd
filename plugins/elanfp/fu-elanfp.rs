// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ValidateStream)]
struct ElanfpFirmwareHdr {
    magic: u32le == 0x46325354,
}
