// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, Parse, Getters)]
#[repr(C, packed)]
struct FuStructLxstouchInterface {
    protocol_name: [char; 8],
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchPanel {
    x_resolution: u16le,
    y_resolution: u16le,
    x_node: u8,
    y_node: u8,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchVersion {
    boot_ver: u16le,
    core_ver: u16le,
    app_ver: u16le,
    param_ver: u16le,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchCrc {
    boot_crc: u32le,
    app_crc: u32le,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchSetter {
    mode: u8,
    event_trigger_type: u8,
}

#[derive(New, Parse, Getters)]
#[repr(C, packed)]
struct FuStructLxstouchGetter {
    ready_status: u8,
    event_ready: u8,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchFlashIapCmd {
    addr: u32le,
    size: u16le,
    status: u8,
    cmd: u8,
}
