// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuAmtStatus {
    Success,
    InternalError,
    NotReady,
    InvalidAmtMode,
    InvalidMessageLength,
}

#[derive(ToString)]
#[repr(u8)]
enum FuAmtProvisioningState {
    Unprovisioned,
    BeingProvisioned,
    Provisioned,
}

#[repr(u32)]
enum FuAmtHostIfCommand {
    ProvisioningModeRequest = 0x04000008,
    ProvisioningStateRequest = 0x04000011,
    CodeVersionsRequest = 0x0400001A,
    ProvisioningModeResponse = 0x04800008,
    ProvisioningStateResponse = 0x04800011,
    CodeVersionsResponse = 0x0480001A,
}

#[derive(New)]
struct FuAmtHostIfMsgCodeVersionRequest {
    version_major: u8 == 0x1,
    version_minor: u8 == 0x1,
    _reserved: u16,
    command: FuAmtHostIfCommand == CodeVersionsRequest,
    length: u32 == 0x0,
}

#[derive(Parse)]
struct FuAmtHostIfMsgCodeVersionResponse {
    version_major: u8 == 0x1,
    version_minor: u8 == 0x1,
    _reserved: u16,
    command: FuAmtHostIfCommand == CodeVersionsResponse,
    _length: u32,
    status: u32,
    _bios: [char; 65],
    version_count: u32,
    // now variable length of FuAmtUnicodeString
}

#[derive(Parse)]
struct FuAmtUnicodeString {
    description_length: u16,
    description_string: [char; 20],
    version_length: u16,
    version_string: [char; 20],
}

#[derive(New)]
struct FuAmtHostIfMsgProvisioningStateRequest {
    version_major: u8 == 0x1,
    version_minor: u8 == 0x1,
    _reserved: u16,
    command: FuAmtHostIfCommand == ProvisioningStateRequest,
    length: u32 == 0x0,
}

#[derive(Parse)]
struct FuAmtHostIfMsgProvisioningStateResponse {
    version_major: u8 == 0x1,
    version_minor: u8 == 0x1,
    _reserved: u16,
    command: FuAmtHostIfCommand == ProvisioningStateResponse,
    length: u32 == 0x8,
    status: u32,
    provisioning_state: FuAmtProvisioningState,
}
