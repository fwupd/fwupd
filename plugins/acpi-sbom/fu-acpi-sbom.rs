// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

use fwupd::uswid::FuUswidHeaderFlags;
use fwupd::uswid::FuUswidPayloadCompression;
use fwupd::uswid::FuUswidPayloadFormat;

// nearly a FuStructUswid (no magic header!) -- but the ACPI spec may diverge over time
#[derive(New, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructAcpiSbomEntry {
    hdrver: u8 = 0x04,
    hdrsz: u16le = $struct_size,
    payloadsz: u32le,
    flags: FuUswidHeaderFlags,
    compression: FuUswidPayloadCompression = None,
    format: FuUswidPayloadFormat,
}

