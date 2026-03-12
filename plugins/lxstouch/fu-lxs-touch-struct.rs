// Copyright 2026 LXS <support@lxsemicon.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(C, packed)]
struct FuStructLxsTouchPanel {
    x_resolution: u16le,
    y_resolution: u16le,
    x_node: u8,
    y_node: u8,
}

#[repr(C, packed)]
struct FuStructLxsTouchVersion {
    boot_ver: u16,
    core_ver: u16,
}

#[repr(C, packed)]
struct FuStructLxsTouchProtocolSetter {
    mode: u8,
    event_trigger_type: u8,
}

#[repr(C, packed)]
struct FuStructLxsTouchProtocolGetter {
    ready_status: u8,
    event_ready: u8,
}

#[repr(C, packed)]
struct FuStructLxsTouchFlashIAPCmd {
    addr: u32le,
    size: u16le,
    status: u8,
    cmd: u8,
}
