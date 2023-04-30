// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct AcpiTable {
    signature: [char; 4],
    length: u32le,
    revision: u8,
    checksum: u8,
    oem_id: [char; 6],
    oem_table_id: [char; 8],
    oem_revision: u32be,
    _asl_compiler_id: [char; 4],
    _asl_compiler_revision: u32le,
}
