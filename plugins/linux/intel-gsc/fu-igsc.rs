// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructIgscOpromVersion {
    major: u16le,
    minor: u16le,
    hotfix: u16le,
    build: u16le,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructIgscFwVersion {
    project: [char; 4], // project code name
    hotfix: u16le,
    build: u16le,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuIgscFwdataDeviceInfo2 {
    subsys_vendor_id: u16le,
    subsys_device_id: u16le,
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuIgscFwdataDeviceInfo4 {
    vendor_id: u16le,
    device_id: u16le,
    subsys_vendor_id: u16le,
    subsys_device_id: u16le,
}

#[derive(ParseStream, Default)]
#[repr(C, packed)]
struct FuStructIgscFwuGwsImageInfo {
    format_version: u32le == 0x1,
    instance_id: u32le,
    _reserved: [u32; 14],
}
// represents a GSC FW sub-partition such as FTPR, RBEP
#[derive(Getters)]
#[repr(C, packed)]
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
#[repr(C, packed)]
struct FuStructIgscFwuIupData {
    iup_name: u32le,
    flags: u16le,
    _reserved: u16le,
    svn: u32le,
    vcn: u32le,
}

#[derive(Getters, Default)]
#[repr(C, packed)]
struct FuStructIgscFwuHeciImageMetadata {
    version_format: u32le = 0x1,
}

#[derive(ParseStream, Default)]
#[repr(C, packed)]
struct FuStructIgscFwuImageMetadataV1 {
    version_format: u32le = 0x1,  // struct IgscFwuHeciImageMetadata
    project: [char; 4],
    version_hotfix: u16le,         // version of the overall IFWI image, i.e. the combination of IPs
    version_build: u16le,
    // struct FuStructIgscFwuFwImageData
    // struct FuStructIgscFwuIupData
}

enum FuIgscFwuHeciPartitionVersion {
    Invalid,
    GfxFw,
    OpromData,
    OpromCode,
}

enum FuIgscFwuHeciPayloadType {
    Invalid,
    GfxFw,
    OpromData,
    OpromCode,
    Fwdata = 5,
}

enum FuIgscFwuHeciCommandId {
    Invalid,
    Start,                  // start firmware updated flow
    Data,                   // send firmware data to device
    End,                    // last command in update
    GetVersion,             // retrieve version of a firmware
    NoUpdate,               // do not wait for firmware update
    GetIpVersion,           // retrieve version of a partition
    GetConfig,              // get hardware config
    Status,                 // get status of most recent update
    GetGfxDataUpdateInfo,   // get signed firmware data info
    GetSubsystemIds,        // get subsystem ids (VID/DID)
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructIgscFwdataVersion {
    oem_manuf_data_version: u32le,
    major_version: u16le,
    major_vcn: u16le,
}

#[repr(C, packed)]
struct FuStructIgscFwdataUpdateExt {
    extension_type: u32le,
    extension_length: u32le,
    oem_manuf_data_version: u32le,
    major_vcn: u16le,
    flags: u16le,
}

enum FuIgscFwuExtType {
    DeviceIds           = 0x25,
    FwdataUpdate        = 0x1D,
    DeviceType          = 0x07,
    SignedPackageInfo   = 0x0F,
    IfwiPartMan         = 0x16,
    DeviceIdArray       = 0x37,
}
