// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ParseStream, New)]
struct CabData {
    checksum: u32le,
    comp: u16le,
    uncomp: u16le,
}

#[repr(u16)]
#[derive(ToString)]
enum CabCompression {
    None = 0x0000,
    Mszip = 0x0001,
    Quantum = 0x0002,
    Lzx = 0x0003,
}

#[repr(u16)]
enum CabFileAttribute {
    None = 0x00,
    Readonly = 0x01,
    Hidden = 0x02,
    System = 0x04,
    Arch = 0x20,
    Exec = 0x40,
    NameUtf8 = 0x80,
}

#[derive(ParseStream, New)]
struct CabFile {
    usize: u32le, // uncompressed
    uoffset: u32le, // uncompressed
    index: u16le,
    date: u16le,
    time: u16le,
    fattr: CabFileAttribute,
}

#[derive(ParseStream, New)]
struct CabFolder {
    offset: u32le,
    ndatab: u16le,
    compression: CabCompression,
}

#[derive(ParseStream, ValidateStream, New)]
struct CabHeader {
    signature: [char; 4] == "MSCF",
    _reserved1: [u8; 4],
    size: u32le, // in bytes
    _reserved2: [u8; 4],
    off_cffile: u32le, // to the first CabFile entry
    _reserved3: [u8; 4],
    version_minor: u8 == 3,
    version_major: u8 == 1,
    nr_folders: u16le = 1,
    nr_files: u16le,
    flags: u16le,
    set_id: u16le,
    idx_cabinet: u16le,
}

#[derive(ParseStream, New)]
struct CabHeaderReserve {
    rsvd_hdr: u16le,
    rsvd_folder: u8,
    rsvd_block: u8,
}
