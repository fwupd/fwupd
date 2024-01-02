// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, ValidateStream, ParseStream)]
struct Fdt {
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
struct FdtReserveEntry {
    address: u64be,
    size: u64be,
}
#[derive(New, Parse)]
struct FdtProp {
    len: u32be,
    nameoff: u32be,
}
