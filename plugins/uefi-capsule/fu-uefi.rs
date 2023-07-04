// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Getters)]
struct EfiUxCapsuleHeader {
    version: u8 == 0x01,
    checksum: u8,
    image_type: u8,
    _reserved: u8,
    mode: u32le,
    x_offset: u32le,
    y_offset: u32le,
}
#[derive(New, Getters)]
struct EfiCapsuleHeader {
    guid: Guid,
    header_size: u32le = $struct_size,
    flags: u32le,
    image_size: u32le,
}

#[derive(ToString)]
#[repr(u32le)]
enum UefiUpdateInfoStatus {
    Unknown,
    AttemptUpdate,
    Attempted,
}

#[derive(New, Parse)]
struct EfiUpdateInfo {
    version: u32le = 0x7,
    guid: Guid,
    flags: u32le,
    hw_inst: u64le,
    time_attempted: [u8; 16], // a EFI_TIME_T
    status: UefiUpdateInfoStatus,
    // EFI_DEVICE_PATH goes here
}
#[derive(Parse)]
struct AcpiInsydeQuirk {
    signature: [char; 6],
    size: u32le,
    flags: u32le,
}
#[derive(ToString, FromString)]
enum UefiDeviceKind {
    Unknown,
    SystemFirmware,
    DeviceFirmware,
    UefiDriver,
    Fmp,
    DellTpmFirmware,
    Last,
}
#[derive(ToString)]
enum UefiDeviceStatus {
    Success = 0x00,
    ErrorUnsuccessful = 0x01,
    ErrorInsufficientResources = 0x02,
    ErrorIncorrectVersion = 0x03,
    ErrorInvalidFormat = 0x04,
    ErrorAuthError = 0x05,
    ErrorPwrEvtAc = 0x06,
    ErrorPwrEvtBatt = 0x07,
    Last,
}
