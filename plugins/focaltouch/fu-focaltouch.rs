// Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuFocaltouchCmd {
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

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructFocaltouchBinLengthReq {
    cmd: u8,
    reg: u8,
    size: u24be,	// size
}

#[derive(Parse, Getters)] 
#[repr(C, packed)]
struct FuStructFocaltouchReadyForUpgradeRes {
    report_id: u8,      // Offset 0
    _pad: [u8; 2],      // Offset 1-2
    len: u8,            // Offset 3
    cmd: u8,            // Offset 4
    status: u8,         // Offset 5
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructFocaltouchUsbReadUpgradeIdRes {
    report_id: u8,      // Offset 0
    _pad: [u8; 2],      // Offset 1-2
    len: u8,            // Offset 3
    cmd: u8,            // Offset 4
    upgrade_id: u16be,  // Offset 5-6
}
