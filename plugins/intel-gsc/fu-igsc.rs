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
    image_data: FuStructIgscFwuFwImageData,
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

#[repr(u8)]
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


#[repr(u8)]
enum FuIgscFwuHeciHdrFlags {
    None,
    IsResponse,
}

#[repr(u32le)]
enum FuIgscFwuHeciStatus {
    Success = 0x0,
    SizeError = 0x5,
    InvalidParams = 0x85,
    InvalidCommand = 0x8D,
    Failure = 0x9E,
    UpdateOpromInvalidStructure = 0x1035,
    UpdateOpromSectionNotExist = 0x1032,
}

// GetIpVersion
#[derive(New, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciVersionReq {
    command_id: FuIgscFwuHeciCommandId == GetIpVersion,
    hdr_flags: FuIgscFwuHeciHdrFlags == None,
    _hdr_reserved: [u8; 2],
    partition: u32le,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciVersionRes {
    command_id: FuIgscFwuHeciCommandId == GetIpVersion,
    hdr_flags: FuIgscFwuHeciHdrFlags == IsResponse,
    _hdr_reserved: [u8; 2],
    status: FuIgscFwuHeciStatus,
    _status_reserved: u32le,
    partition: u32le,
    version_length: u32le,
    // version
}

// GetGfxDataUpdateInfo
#[derive(New, Default)]
#[repr(C, packed)]
struct FuIgscFwDataHeciVersionReq {
    command_id: FuIgscFwuHeciCommandId == GetGfxDataUpdateInfo,
    hdr_flags: FuIgscFwuHeciHdrFlags == None,
    _hdr_reserved: [u8; 2],
    _reserved: [u32; 2],
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuIgscFwDataHeciVersionRes {
    command_id: FuIgscFwuHeciCommandId == GetGfxDataUpdateInfo,
    hdr_flags: FuIgscFwuHeciHdrFlags == IsResponse,
    _hdr_reserved: [u8; 2],
    status: FuIgscFwuHeciStatus,
    _status_reserved: u32le,
    _format_version: u32le,
    oem_version_nvm: u32le,
    oem_version_fitb: u32le,
    major_version: u16le,
    major_vcn: u16le,
    oem_version_fitb_valid: u32le,
    _flags: u32le,
    _reserved: [u32; 7],
}

// GetSubsystemIds
#[derive(New, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciGetSubsystemIdsReq {
    command_id: FuIgscFwuHeciCommandId == GetSubsystemIds,
    hdr_flags: FuIgscFwuHeciHdrFlags == None,
    _hdr_reserved: [u8; 2],
    _reserved: [u32le; 2],
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciGetSubsystemIdsRes {
    command_id: FuIgscFwuHeciCommandId == GetSubsystemIds,
    hdr_flags: FuIgscFwuHeciHdrFlags == IsResponse,
    _hdr_reserved: [u8; 2],
    status: FuIgscFwuHeciStatus,
    _status_reserved: u32le,
    ssvid: u16le,
    ssdid: u16le,
    _reserved: [u32le; 2],
}

// GetConfig
#[derive(New, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciGetConfigReq {
    command_id: FuIgscFwuHeciCommandId == GetConfig,
    hdr_flags: FuIgscFwuHeciHdrFlags == None,
    _hdr_reserved: [u8; 2],
    _reserved: [u32le; 2],
}

#[repr(u32le)]
enum FuIgscFwuHeciGetConfigFlags {
    None,
    OpromCodeDevidEnforcement,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciGetConfigRes {
    command_id: FuIgscFwuHeciCommandId == GetConfig,
    hdr_flags: FuIgscFwuHeciHdrFlags == IsResponse,
    _hdr_reserved: [u8; 2],
    status: FuIgscFwuHeciStatus,
    _status_reserved: u32le,
    _format_version: u32le,
    _hw_step: u32le,
    hw_sku: u32le,
    flags: FuIgscFwuHeciGetConfigFlags,
    _reserved: [u32; 7],
    _debug_config: u32le,
}

// End
#[derive(New, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciEndReq {
    command_id: FuIgscFwuHeciCommandId == End,
    hdr_flags: FuIgscFwuHeciHdrFlags == None,
    _hdr_reserved: [u8; 2],
    _reserved: u32le,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciEndRes {
    command_id: FuIgscFwuHeciCommandId == End,
    hdr_flags: FuIgscFwuHeciHdrFlags == IsResponse,
    _hdr_reserved: [u8; 2],
    status: FuIgscFwuHeciStatus,
    _status_reserved: u32le,
}

// Data
#[derive(New, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciDataReq {
    command_id: FuIgscFwuHeciCommandId == Data,
    hdr_flags: FuIgscFwuHeciHdrFlags == None,
    _hdr_reserved: [u8; 2],
    data_length: u32le,
    _reserved: u32le,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciDataRes {
    command_id: FuIgscFwuHeciCommandId == Data,
    hdr_flags: FuIgscFwuHeciHdrFlags == IsResponse,
    _hdr_reserved: [u8; 2],
    status: FuIgscFwuHeciStatus,
    _status_reserved: u32le,
}

// Start
#[repr(u32le)]
enum FuIgscFwuHeciStartFlags {
    None,
    ForceUpdate,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciStartReq {
    command_id: FuIgscFwuHeciCommandId == Start,
    hdr_flags: FuIgscFwuHeciHdrFlags == None,
    _hdr_reserved: [u8; 2],
    update_img_length: u32le,
    payload_type: u32le,
    flags: FuIgscFwuHeciStartFlags,
    _reserved: [u32le; 8],
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciStartRes {
    command_id: FuIgscFwuHeciCommandId == Start,
    hdr_flags: FuIgscFwuHeciHdrFlags == IsResponse,
    _hdr_reserved: [u8; 2],
    status: FuIgscFwuHeciStatus,
    _status_reserved: u32le,
}

// NoUpdate
#[derive(New, Default)]
#[repr(C, packed)]
struct FuIgscFwuHeciNoUpdateReq {
    command_id: FuIgscFwuHeciCommandId == NoUpdate,
    hdr_flags: FuIgscFwuHeciHdrFlags == None,
    _hdr_reserved: [u8; 2],
    _reserved: u32le,
}

// blobs
#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructIgscFwdataVersion {
    oem_manuf_data_version: u32le,
    major_version: u16le,
    major_vcn: u16le,
    key_index: u8,
    _reserved1: [u8; 3],
    data_arb_svn: u32le,
    _reserved2: [u8; 16],
}

#[repr(C, packed)]
struct FuStructIgscFwdataUpdateExt {
    extension_type: u32le,
    extension_length: u32le,
    oem_manuf_data_version: u32le,
    major_vcn: u16le,
    flags: u16le,
}

#[derive(ToString)]
enum FuIgscFwuExtType {
    DeviceType          = 7,
    ModuleAttr          = 10,
    SignedPackageInfo   = 15,
    FwdataUpdate        = 29,
    IfwiPartMan         = 22,
    DeviceIdArray       = 37,
}
