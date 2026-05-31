// Copyright 2026 Sean Rhodes <sean@starlabs.systems>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u32le)]
enum FuCorebootCfrFlags {
    None = 0x0,
    Readonly = 1 << 0,
}

#[repr(u16le)]
enum FuCorebootCfrType {
    Enum = 1,
    Number = 2,
    Bool = 3,
}

#[repr(u32le)]
enum FuCorebootCfrApplyMethod {
    None,
    ApmCnt,
}

#[derive(ParseBytes, ValidateStream, Default, New)]
#[repr(C, packed)]
struct FuStructCorebootCfrSettingsHeader {
    magic: u32le == 0x44574643, // CFWD
    version: u16le == 0x0002,
    header_size: u16le == 0x0010,
    size: u32le,
    record_count: u32le,
}

#[derive(ParseBytes, ValidateStream, Default, New)]
#[repr(C, packed)]
struct FuStructCorebootCfrOptionRecord {
    kind: FuCorebootCfrType,
    header_size: u16le == 0x003c,
    cfr_flags: FuCorebootCfrFlags,
    object_id: u64le,
    runtime_apply_method: FuCorebootCfrApplyMethod,
    runtime_apply_id: u32le,
    default_value: u32le,
    min: u32le,
    max: u32le,
    step: u32le,
    display_flags: u32le,
    name_size: u32le,
    ui_name_size: u32le,
    help_text_size: u32le,
    enum_count: u32le,
}

#[derive(ParseBytes, New)]
#[repr(C, packed)]
struct FuStructCorebootCfrEnumRecord {
    value: u32le,
    ui_name_size: u32le,
}
