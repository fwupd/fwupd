// Copyright 2026 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, Parse, Getters, Setters, Default)]
#[repr(C, packed)]
struct FuStructAmdAfcEieHeader {
    signature: u32le == 0x48434641,
    length: u32le,
    revision: u16le,
    reserved0: u8,
    checksum: u8,
    reserved1: [u8; 4],
    forms_offset: u32le,
    forms_size: u32le,
    strings_offset: u32le,
    strings_size: u32le,
    varstores_offset: u32le,
    varstores_size: u32le,
    reserved2: [u8; 10],
}

#[derive(New, Parse, Getters, Setters, Default)]
#[repr(C, packed)]
struct FuStructAmdAfcConfigHeader {
    signature: u32le == 0x48434641,
    length: u32le,
    revision: u16le = 0x0100,
    reserved: u8,
    checksum: u8,
    strings_size: u32le,
    entry_count: u16le,
}

#[derive(New, Setters)]
#[repr(C, packed)]
struct FuStructAmdAfcConfigId {
    value: u16le,
}

#[derive(New, Parse, Getters, Setters, Default)]
#[repr(C, packed)]
struct FuStructAmdAfcVarstoreHeader {
    length: u32le,
    name_size: u8,
    reserved: [u8; 16],
    id: u16le,
    data_size: u32le,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructAmdAfcHiiPackageHeader {
    length: u24le,
    kind: u8,
}
