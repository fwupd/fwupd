// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(FromString)]
enum FuReleasePriority {
    None,
    Local,
    Remote,
}

#[derive(FromString)]
enum FuP2pPolicy {
    Nothing = 0x00,
    Metadata = 0x01,
    Firmware = 0x02,
}

#[derive(ToString)]
enum FuEngineInstallPhase {
    Setup,
    Install,
    Attach,
    Detach,
    Prepare,
    Cleanup,
    Reload,
    CompositePrepare,
    CompositeCleanup,
}

#[derive(ToBitString)]
enum FuEngineRequestFlag {
    None = 0,
    NoRequirements = 1 << 0,
    AnyRelease = 1 << 1,
}

#[derive(ToBitString)]
enum FuIdleInhibit {
    None = 0,
    Timeout = 1 << 0,
    Signals = 1 << 1,
}

enum FuClientFlag {
    None = 0,
    Active = 1 << 0,
}

#[derive(ParseBytes)]
struct FuStructUdevMonitorNetlinkHeader {
    prefix: [char; 8] == "libudev",
    magic: u32be == 0xFEEDCAFE,
    header_size: u32le,
    properties_off: u32le,
    properties_len: u32le,
    filter_subsystem_hash: u32le,
    filter_devtype_hash: u32le,
    filter_tag_bloom_hi: u32le,
    filter_tag_bloom_lo: u32le,
}

enum FuUdevMonitorNetlinkGroup {
    None,
    Kernel,
    Udev,
}

#[derive(FromString)]
enum FuUdevAction {
    Unknown,
    Add,
    Remove,
    Change,
    Move,
    Online,
    Offline,
    Bind,
    Unbind,
}
