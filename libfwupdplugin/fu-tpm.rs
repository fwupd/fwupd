// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString, FromString)]
#[repr(u32le)]
enum FuTpmAlg {
    Unknown             = 0x0000,
    Sha1                = 0x0004,
    Sha256              = 0x000B,
    Sha384              = 0x000C,
    Sha512              = 0x000D,
    Sm3_256             = 0x0012,
    Sha3_256            = 0x0027,
    Sha3_384            = 0x0028,
    Sha3_512            = 0x0029,
}

#[derive(ToString, FromString)]
#[repr(u32le)]
enum FuTpmEventlogItemKind {
    PrebootCert = 0x00000000,
    PostCode = 0x00000001,
    NoAction = 0x00000003,
    Separator = 0x00000004,
    Action = 0x00000005,
    EventTag = 0x00000006,
    SCrtmContents = 0x00000007,
    SCrtmVersion = 0x00000008,
    CpuMicrocode = 0x00000009,
    PlatformConfigFlags = 0x0000000a,
    TableOfDevices = 0x0000000b,
    CompactHash = 0x0000000c,
    NonhostCode = 0x0000000f,
    NonhostConfig = 0x00000010,
    NonhostInfo = 0x00000011,
    OmitBootDeviceEvents = 0x00000012,
    EfiEventBase = 0x80000000,
    EfiVariableDriverConfig = 0x80000001,
    EfiVariableBoot = 0x80000002,
    EfiBootServicesApplication = 0x80000003,
    EfiBootServicesDriver = 0x80000004,
    EfiRuntimeServicesDriver = 0x80000005,
    EfiGptEvent = 0x80000006,
    EfiAction = 0x80000007,
    EfiPlatformFirmwareBlob = 0x80000008,
    EfiHandoffTables = 0x80000009,
    EfiHcrtmEvent = 0x80000010,
    EfiVariableAuthority = 0x800000e0,
}

#[derive(ParseBytes, Default)]
#[repr(C, packed)]
struct FuStructTpmEfiStartupLocalityEvent {
    signature: [char; 16] == "StartupLocality",
    locality: u8,    // from which TPM2_Startup() was issued -- which is the initial value of PCR0
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuStructTpmEventLog1Item {
    pcr: u32le,
    type: FuTpmEventlogItemKind,
    digest: [u8; 20],
    datasz: u32le,
    // data: [u8; datasz],
}

#[derive(ParseStream, Default, New)]
#[repr(C, packed)]
struct FuStructTpmEventLog2Hdr {
    pcr: u32le == 0x0,
    type: FuTpmEventlogItemKind == NoAction,
    _digest: [u8; 20],
    datasz: u32le = 15,
    data: [char; 15] == "Spec ID Event03",
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuStructTpmEventLog2 {
    pcr: u32le,
    type: FuTpmEventlogItemKind,
    digest_count: u32le,
}
