// Copyright 2026 Realtek Corporation
// Copyright 2026 Shadow Zhang <shadow_zhang@realsil.com.cn>
// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// vendor bRequest codes
enum FuRealtekBillboardRqt {
    SetRegister = 0x01, // write MCU register
    GetRegister = 0x02, // read MCU register
    SendData = 0x03,    // send firmware data
    WriteFlash = 0x40,
    ReadFlash = 0x41,
    SectorErase = 0x42,
    BankErase = 0x43,
    IspEnable = 0x44,
    DualBank = 0x45,
    Handshake = 0x50,
}

// MCU register addresses
enum FuRealtekBillboardMcuReg {
    FwFlashPortAcc = 0x6D, // FW flash port access
    Usb = 0xEE,            // USB control register
}

// direct register addresses for firmware version
enum FuRealtekBillboardReg {
    FwVersion = 0x0004, // [5:0] = FW version
    FwSubVersion = 0x0007, // [7:0] = FW sub version
}

// opcodes for dual bank requests (used as wValue)
enum FuRealtekBillboardDualBankOp {
    GetStartAddr = 0x02,
    GetFlagAddr = 0x04,
}
