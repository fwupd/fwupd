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

#[repr(u8)]
enum FuAmdAfcHiiStringBlockKind {
    End = 0x00,
    StringScsu = 0x10,
    StringScsuFont = 0x11,
    StringsScsu = 0x12,
    StringsScsuFont = 0x13,
    StringUcs2 = 0x14,
    StringUcs2Font = 0x15,
    StringsUcs2 = 0x16,
    StringsUcs2Font = 0x17,
    Duplicate = 0x20,
    Skip2 = 0x21,
    Skip1 = 0x22,
    Ext1 = 0x30,
    Ext2 = 0x31,
    Ext4 = 0x32,
    Font = 0x40,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrForm {
    opcode: u8,
    length_scope: u8,
    id: u16le,
    title: u16le,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrQuestion {
    opcode: u8,
    length_scope: u8,
    prompt: u16le,
    help: u16le,
    id: u16le,
    varstore_id: u16le,
    varstore_offset: u16le,
    flags: u8,
    value_type: u8,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrFormSet {
    opcode: u8,
    length_scope: u8,
    guid: Guid,
    title: u16le,
    help: u16le,
    flags: u8,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrRef {
    opcode: u8,
    length_scope: u8,
    prompt: u16le,
    help: u16le,
    question_id: u16le,
    varstore_id: u16le,
    varstore_offset: u16le,
    question_flags: u8,
    form_id: u16le,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrGuid {
    opcode: u8,
    length_scope: u8,
    guid: Guid,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrOption {
    opcode: u8,
    length_scope: u8,
    string_id: u16le,
    flags: u8,
    value_type: u8,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrDefault {
    opcode: u8,
    length_scope: u8,
    id: u16le,
    value_type: u8,
}
