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
enum FuEngineEmulatorPhase {
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

enum FuEngineLoadFlags {
    None = 0,
    Readonly = 1 << 0,
    Coldplug = 1 << 1,
    Remotes = 1 << 2,
    Hwinfo = 1 << 3,
    NoCache = 1 << 4,
    NoIdleSources = 1 << 5,
    BuiltinPlugins = 1 << 6,
    EnsureClientCert = 1 << 7,
    ExternalPlugins = 1 << 8,   // dload'ed plugins such as flashrom
    DeviceHotplug = 1 << 9,
    ColdplugForce = 1 << 10,    // even without a matched plugin
    Ready = 1 << 11,
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

#[derive(FromString)]
enum FuEngineCapabilityFlag {
    Unknown = 0,
    IdRequirementGlob = 1 << 0,
}

#[derive(ParseBytes, Default)]
#[repr(C, packed)]
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
