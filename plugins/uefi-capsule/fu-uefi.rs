// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructEfiUxCapsuleHeader {
    version: u8 == 0x01,
    checksum: u8,
    image_type: u8,
    _reserved: u8,
    mode: u32le,
    x_offset: u32le,
    y_offset: u32le,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructEfiCapsuleHeader {
    guid: Guid,
    header_size: u32le = $struct_size,
    flags: u32le,
    image_size: u32le,
}

#[derive(ToString, FromString)]
#[repr(u32le)]
enum FuUefiUpdateInfoStatus {
    Unknown,
    AttemptUpdate,
    Attempted,
}

#[derive(New, Parse, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructEfiUpdateInfo {
    version: u32le = 0x7,
    guid: Guid,
    flags: u32le,
    hw_inst: u64le,
    time_attempted: [u8; 16], // a EFI_TIME_T
    status: FuUefiUpdateInfoStatus,
    // EFI_DEVICE_PATH goes here
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructAcpiInsydeQuirk {
    signature: [char; 6],
    size: u32le,
    flags: u32le,
}

#[derive(ToString, FromString)]
enum FuUefiCapsuleDeviceKind {
    Unknown,
    SystemFirmware,
    DeviceFirmware,
    UefiDriver,
    Fmp,
    DellTpmFirmware,
}

#[derive(ToString)]
enum FuUefiCapsuleDeviceStatus {
    Success,
    ErrorUnsuccessful,
    ErrorInsufficientResources,
    ErrorIncorrectVersion,
    ErrorInvalidFormat,
    ErrorAuthError,
    ErrorPwrEvtAc,
    ErrorPwrEvtBatt,
}

#[derive(ParseStream, Default)]
#[repr(C, packed)]
struct FuStructBitmapFileHeader {
    signature: [char; 2] == "BM",
    size: u32le,
    _reserved1: u16le,
    _reserved2: u16le,
    _image_offset: u32le,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructBitmapInfoHeader {
    _header_size: u32le,
    width: u32le,
    height: u32le,
}

#[repr(u32le)]
enum FuAbIndicationsValue {
    Unknown,
    Revert,
    Accept,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructAbIndications {
    value: FuAbIndicationsValue = Accept,
}
