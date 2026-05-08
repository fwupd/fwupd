// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructAcpiTable {
    signature: [char; 4],
    length: u32le,
    revision: u8,
    checksum: u8,
    oem_id: [char; 6],
    oem_table_id: [char; 8],
    oem_revision: u32be,
    creator_id: [char; 4],
    creator_revision: u32le,
}

// ACPI Fixed ACPI Description Table (FADT) Preferred_PM_Profile values.
#[repr(u8)]
enum FuAcpiFadtPmProfile {
    Unspecified,
    Desktop,
    Mobile,
    Workstation,
    EnterpriseServer,
    SohoServer,
    AppliancePc,
    PerformanceServer,
    Tablet,
}
