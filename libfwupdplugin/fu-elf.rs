// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[repr(u16le)]
enum ElfFileHeaderType {
    None = 0x00,
    Rel = 0x01,
    Exec = 0x02,
    Dyn = 0x03,
    Core = 0x04,
}

#[derive(Parse, Validate)]
struct ElfFileHeader64le {
    ei_magic: [char; 4]: const="\x7F\x45\x4C\x46",
    ei_class: u8: const=0x2, // 64-bit format
    ei_data: u8: const=0x1, // LE
    ei_version: u8: const=0x1,
    ei_osabi: u8: default=0x3,
    ei_abiversion: u8,
    _ei_padding: [u8; 7]: default=0x00000000000000,
    type: ElfFileHeaderType,
    machine: u16le,
    version: u32le: const=0x1,
    entry: u64le: default=0x0,
    phoff: u64le: default=$struct_size,
    shoff: u64le: default=0x0,
    flags: u32le: default=0x0,
    ehsize: u16le: default=$struct_size,
    phentsize: u16le: default=0x0,
    phnum: u16le: default=0x0,
    shentsize: u16le: default=0x0,
    shnum: u16le: default=0x0,
    shstrndx: u16le: default=0x0,
}

#[derive(Parse)]
struct ElfProgramHeader64le {
    flags: u32le,
    offset: u64le,
    vaddr: u64le,
    paddr: u64le,
    filesz: u64le: default=0x0,
    memsz: u64le: default=0x0,
    flags2: u32le,
    align: u64le: default=0x0,
}

#[repr(u32le)]
enum ElfSectionHeaderType {
    Null = 0x0,
    Progbits = 0x1,
    Symtab = 0x2,
    Strtab = 0x3,
    Rela = 0x4,
    Hash = 0x5,
    Dynamic = 0x6,
    Note = 0x7,
    Nobits = 0x8,
    Rel = 0x9,
    Shlib = 0x0a,
    Dynsym = 0x0b,
    InitArray = 0x0e,
    FiniArray = 0x0f,
    PreinitArray = 0x10,
    Group = 0x11,
    SymtabShndx = 0x12,
    Num = 0x13,
}

#[derive(Parse)]
struct ElfSectionHeader64le {
    name: u32le,
    type: ElfSectionHeaderType,
    flags: u64le,
    addr: u64le: default=0x0,
    offset: u64le: default=0x0,
    size: u64le: default=0x0,
    link: u32le: default=0x0,
    info: u32le: default=0x0,
    addralign: u64le: default=0x0,
    entsize: u64le: default=0x0,
}
