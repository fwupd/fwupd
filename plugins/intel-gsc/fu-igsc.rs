// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Parse)]
struct FuStructIgscOpromVersion {
    major: u16le,
    minor: u16le,
    hotfix: u16le,
    build: u16le,
}

#[derive(New, Getters)]
struct FuStructIgscFwVersion {
    project: [char; 4], // project code name
    hotfix: u16le,
    build: u16le,
}

#[derive(ParseStream)]
struct FuStructIgscOpromSubsystemDeviceId {
    subsys_vendor_id: u16le,
    subsys_device_id: u16le,
}

#[derive(ParseStream)]
struct FuStructIgscOpromSubsystemDevice4Id {
    vendor_id: u16le,
    device_id: u16le,
    subsys_vendor_id: u16le,
    subsys_device_id: u16le,
}

#[derive(ParseStream, Default)]
struct FuStructIgscFwuGwsImageInfo {
    format_version: u32le == 0x1,
    instance_id: u32le,
    _reserved: [u32; 14],
}
/* represents a GSC FW sub-partition such as FTPR, RBEP */
#[derive(Getters)]
struct FuStructIgscFwuFwImageData {
    version_major: u16le,
    version_minor: u16le,
    version_hotfix: u16le,
    version_build: u16le,
    flags: u16le,
    fw_type: u8,
    fw_sub_type: u8,
    arb_svn: u32le,
    tcb_svn: u32le,
    vcn: u32le,
}

#[derive(Getters)]
struct FuStructIgscFwuIupData {
    iup_name: u32le,
    flags: u16le,
    _reserved: u16le,
    svn: u32le,
    vcn: u32le,
}

#[derive(Getters, Default)]
struct FuStructIgscFwuHeciImageMetadata {
    version_format: u32le = 0x1,
}

#[derive(ParseStream, Default)]
struct FuStructIgscFwuImageMetadataV1 {
    version_format: u32le = 0x1,  // struct IgscFwuHeciImageMetadata
    project: [char; 4],
    version_hotfix: u16le,         // version of the overall IFWI image, i.e. the combination of IPs
    version_build: u16le,
    // struct FuStructIgscFwuFwImageData
    // struct FuStructIgscFwuIupData
}
