// Copyright 2026 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ParseBytes, Getters, Setters, Default)]
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

#[derive(New, Parse, Default)]
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

#[derive(New)]
#[repr(C, packed)]
struct FuStructAmdAfcConfigId {
    value: u16le,
}

#[derive(New, ParseBytes, Default)]
#[repr(C, packed)]
struct FuStructAmdAfcVarstoreHeader {
    length: u32le,
    name_size: u8,
    reserved: [u8; 16],
    id: u16le,
    data_size: u32le,
}

#[repr(u8)]
enum FuAmdAfcHiiPackageKind {
    Forms = 0x02,
    Strings = 0x04,
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructAmdAfcHiiPackageHeader {
    length: u24le,
    kind: FuAmdAfcHiiPackageKind,
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

#[repr(u8)]
enum FuAmdAfcIfrOpcode {
    Form = 0x01,
    OneOf = 0x05,
    Checkbox = 0x06,
    Numeric = 0x07,
    OneOfOption = 0x09,
    FormSet = 0x0e,
    Ref = 0x0f,
    End = 0x29,
    Default = 0x5b,
    Guid = 0x5f,
}

#[derive(ToString)]
enum FuAmdAfcSettingKind {
    Enumeration,
    Integer,
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrForm {
    opcode: u8,
    length_scope: u8,
    id: u16le,
    title: u16le,
}

#[derive(ParseBytes)]
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

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrFormSet {
    opcode: u8,
    length_scope: u8,
    guid: Guid,
    title: u16le,
    help: u16le,
    flags: u8,
}

#[repr(u8)]
enum FuAmdAfcQuestionFlags {
    None = 0x00,
    Readonly = 0x01,
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrRef {
    opcode: u8,
    length_scope: u8,
    prompt: u16le,
    help: u16le,
    question_id: u16le,
    varstore_id: u16le,
    varstore_offset: u16le,
    question_flags: FuAmdAfcQuestionFlags,
    form_id: u16le,
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrGuid {
    opcode: u8,
    length_scope: u8,
    guid: Guid,
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrOption {
    opcode: u8,
    length_scope: u8,
    string_id: u16le,
    flags: u8,
    value_type: u8,
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructAmdAfcIfrDefault {
    opcode: u8,
    length_scope: u8,
    id: u16le,
    value_type: u8,
}
