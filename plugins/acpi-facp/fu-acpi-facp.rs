// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

use fwupd::acpi_table::FuAcpiFadtPmProfile;

#[repr(u32le)]
enum FuAcpiFacpFlags {
    None = 0,
    LowPowerS0IdleCapable = 1 << 21,
}

#[derive(New, ParseStream, ToBytes)]
#[repr(C, packed)]
struct FuStructAcpiFacp {
    firmware_ctrl: u32le,
    dsdt: u32le,
    reserved1: u8,
    pm_profile: FuAcpiFadtPmProfile,
    reserved2: [u8; 66],
    flags: FuAcpiFacpFlags,
}
