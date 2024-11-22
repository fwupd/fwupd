// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuFdtToken {
    BeginNode   = 0x00000001,
    EndNode     = 0x00000002,
    Prop        = 0x00000003,
    Nop         = 0x00000004,
    End         = 0x00000009,
}

#[derive(New, ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructFdt {
    magic: u32be == 0xD00DFEED,
    totalsize: u32be,
    off_dt_struct: u32be,
    off_dt_strings: u32be,
    off_mem_rsvmap: u32be,
    version: u32be,
    last_comp_version: u32be = 2,
    boot_cpuid_phys: u32be,
    size_dt_strings: u32be,
    size_dt_struct: u32be,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructFdtReserveEntry {
    address: u64be,
    size: u64be,
}

#[derive(New, Parse)]
#[repr(C, packed)]
struct FuStructFdtProp {
    len: u32be,
    nameoff: u32be,
}
