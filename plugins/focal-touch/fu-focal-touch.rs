// Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuFocalTouchRegister {
    FwVersion1 = 0xA6,
    FwVersion2 = 0xAD,
    VerifyId1 = 0x9F,
    VerifyId2 = 0xA3,
}

#[repr(u8)]
enum FuFocalTouchCmd {
    EnterUpgradeMode = 0x40,
    CheckCurrentState = 0x41,
    ReadyForUpgrade = 0x42,
    SendData = 0x43,
    UpgradeChecksum = 0x44,
    ExitUpgradeMode = 0x45,
    UsbReadUpgradeId = 0x46,
    UsbEraseFlash = 0x47,
    UsbBootRead = 0x48,
    UsbBootBootloaderversion = 0x49,
    ReadRegister = 0x50,
    WriteRegister = 0x51,
    BinLength = 0x7A,
    Ack = 0xF0,
    Nack = 0xFF,
}

#[repr(u8)]
enum FuFocalTouchUcMode {
    Upgrade = 1,
    Runtime = 2,
}

#[repr(u8)]
enum FuFocalTouchPacketType {
    First,
    Mid,
    End,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchReadRegisterReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == ReadRegister,
    address: FuFocalTouchRegister,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchReadRegisterRes {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == ReadRegister,
    _address: FuFocalTouchRegister,
    value: u8,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchSendDataReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8,
    cmd: FuFocalTouchCmd == SendData,
    packet_type: FuFocalTouchPacketType,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchBinLengthRes {
    report_id: u8 == 0x06,
    _pad: [u8; 2],
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == Ack,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchBinLengthReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == WriteRegister,
    reg: FuFocalTouchCmd == BinLength,
    size: u24be,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchEnterUpgradeModeReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == EnterUpgradeMode,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchEnterUpgradeModeRes {
    report_id: u8 == 0x06,
    _pad: [u8; 2],
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == Ack,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchCheckCurrentStateReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == CheckCurrentState,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchCheckCurrentStateRes {
    report_id: u8 == 0x06,
    _pad: [u8; 2],
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == CheckCurrentState,
    mode: FuFocalTouchUcMode,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchUsbReadUpgradeIdReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == UsbReadUpgradeId,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchUsbReadUpgradeIdRes {
    report_id: u8 == 0x06,
    _pad: [u8; 2],
    len: u8,
    cmd: FuFocalTouchCmd == UsbReadUpgradeId,
    upgrade_id: u16be,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchUsbEraseFlashReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == UsbEraseFlash,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchUpgradeChecksumReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == UpgradeChecksum,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchUpgradeChecksumRes {
    report_id: u8 == 0x06,
    _pad: [u8; 2],
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == UpgradeChecksum,
    value: u32le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchExitUpgradeModeReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == ExitUpgradeMode,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchExitUpgradeModeRes {
    report_id: u8 == 0x06,
    _pad: [u8; 2],
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == Ack,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchReadyForUpgradeReq {
    report_id: u8 == 0x06,
    _pad: [u8; 2] == 0xFFFF,
    len: u8 == $struct_size,
    cmd: FuFocalTouchCmd == ReadyForUpgrade,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalTouchReadyForUpgradeRes {
    report_id: u8 == 0x06,
    _pad: [u8; 2],
    len: u8,
    cmd: FuFocalTouchCmd == ReadyForUpgrade,
    status: u8,
}
