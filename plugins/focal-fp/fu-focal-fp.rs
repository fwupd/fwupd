// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
#[repr(u8)]
enum FuFocalFpCmd {
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
    Ack = 0xF0,
    Nack = 0xFF,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructFocalFpHidRsp {
    _reserved0: u8,
    _reserved1: u8,
    _reserved2: u8,
    length: u8,
    cmd: FuFocalFpCmd,
    // data: [u8; length],
    // csum: u8,
}
