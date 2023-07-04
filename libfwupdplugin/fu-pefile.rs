// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(Parse, Validate)]
struct PeDosHeader {
    magic: u16le == 0x5A4D,
    _cblp: u16le = 0x90,
    _cp: u16le = 0x3,
    _crlc: u16le = 0x0,
    _cparhdr: u16le = 0x4,
    _minalloc: u16le = 0x0,
    _maxalloc: u16le = 0xFFFF,
    _ss: u16le = 0x0,
    _sp: u16le = 0xB8,
    _csum: u16le = 0x0,
    _ip: u16le = 0x0,
    _cs: u16le = 0x0,
    _lfarlc: u16le = 0x40,
    _ovno: u16le = 0x0,
    _res: [u8; 8] = 0x00,
    _oemid: u16le = 0x0,
    _oeminfo: u16le = 0x0,
    _res2: [u8; 20] = 0x00,
    lfanew: u32le = 0x80,
}

#[repr(u16le)]
enum PeCoffMachine {
    Unknown = 0x0,
    Alpha = 0x184,
    Alpha64 = 0x284,
    Am33 = 0x1d3,
    Amd64 = 0x8664,
    Arm = 0x1c0,
    Arm64 = 0xaa64,
    Armnt = 0x1c4,
    Axp64 = 0x284,
    Ebc = 0xebc,
    I386 = 0x14c,
    Ia64 = 0x200,
    Loongarch32 = 0x6232,
    Loongarch64 = 0x6264,
    M32r = 0x9041,
    Mips16 = 0x266,
    Mipsfpu = 0x366,
    Mipsfpu16 = 0x466,
    Powerpc = 0x1f0,
    Powerpcfp = 0x1f1,
    R4000 = 0x166,
    Riscv32 = 0x5032,
    Riscv64 = 0x5064,
    Riscv128 = 0x5128,
    Sh3 = 0x1a2,
    Sh3dsp = 0x1a3,
    Sh4 = 0x1a6,
    Sh5 = 0x1a8,
    Thumb = 0x1c2,
    Wcemipsv2 = 0x169,
}

#[repr(u16le)]
enum PeCoffMagic {
    Pe32 = 0x10b,
    Pe32Plus = 0x20b,
}

#[repr(u16le)]
enum FuCoffSubsystem {
    Unknown = 0,
    Native = 1,
    WindowsGui = 2,
    WindowsCui = 3,
    Os2Cui = 5,
    PosixCui = 7,
    NativeWindows = 8,
    WindowsCeGui = 9,
    EfiApplication = 10,
    EfiBootServiceDriver = 11,
    EfiRuntimeDriver = 12,
    EfiRom = 13,
    Xbox = 14,
    WindowsBootApplication = 16,
}

#[derive(Parse)]
struct PeCoffFileHeader {
    signature: u32le == 0x4550, // "PE\0\0"
    machine: PeCoffMachine = Amd64,
    number_of_sections: u16le = 0,
    _time_date_stamp: u32le = 0,
    pointer_to_symbol_table: u32le = 0,
    number_of_symbols: u32le = 0,
    size_of_optional_header: u16le = 0xf0,
    characteristics: u16le = 0x2022,
}

#[derive(Parse)]
struct PeCoffOptionalHeader64 {
    magic: PeCoffMagic = Pe32Plus,
    _major_linker_version: u8 = 0x0e,
    _minor_linker_version: u8 = 0x0e,
    size_of_code: u32le = 0x0,
    size_of_initialized_data: u32le = 0x0,
    size_of_uninitialized_data: u32le = 0x0,
    addressofentrypoint: u32le = 0x0,
    base_of_code: u32le = 0x0,
    image_base: u64le = 0x0,
    section_alignment: u32le = 0x200,
    file_alignment: u32le = 0x200,
    _major_operating_system_version: u16le = 0x0,
    _minor_operating_system_version: u16le = 0x0,
    _major_image_version: u16le = 0x0,
    _minor_image_version: u16le = 0x0,
    _major_subsystem_version: u16le = 0x0,
    _minor_subsystem_version: u16le = 0x0,
    _win32_versionvalue: u32le = 0x0,
    size_of_image: u32le = 0x0,
    size_of_headers: u32le = 0x0,
    check_sum: u32le = 0x0,
    subsystem: FuCoffSubsystem = EfiApplication,
    _dll_characteristics: u16le = 0x0,
    _size_of_stackreserve: u64le = 0x0,
    _size_of_stack_commit: u64le = 0x0,
    _size_of_heap_reserve: u64le = 0x0,
    _size_of_heap_commit: u64le = 0x0,
    loader_flags: u32le = 0x0,
    number_of_rva_and_sizes: u32le = 0x0,
}

struct PeCoffSymbol {
    name: [char; 8],
    value: u32le,
    section_number: u16le,
    type: u16le,
    storage_class: u8,
    number_of_aux_symbols: u8,
}

#[derive(Parse)]
struct PeCoffSection {
    name: [char; 8],
    virtual_size: u32le = 0x0,
    virtual_address: u32le = 0x0,
    size_of_raw_data: u32le = 0x0,
    pointer_to_raw_data: u32le = 0x0,
    _pointer_to_relocations: u32le = 0x0,
    _pointer_to_linenumbers: u32le = 0x0,
    _number_of_relocations: u16le = 0x0,
    _number_of_linenumbers: u16le = 0x0,
    characteristics: u32le = 0x0,
}
