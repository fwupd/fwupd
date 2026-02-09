/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

enum FuHimaxtpMapcode {
    FwCid = 0x10000000,
    FwVer = 0x10000100,
    CfgVer = 0x10000600,
    IcId = 0x10000300,
    IcIdMod = 0x10000200,
}

enum FuHimaxtpUpdateErrorCode {
    NoError = 0x77,
    McuE0 = 0x00,
    McuE1 = 0xA0,
    NoBl = 0xC1,
    NoMain = 0xC2,
    Bl = 0xB2,
    Pw = 0xB3,
    EraseFlash = 0xB4,
    FlashProgramming = 0xB5,
    NoDevice = 0xFFFFFF00,
    LoadFwBin = 0xFFFFFF01,
    Initial = 0xFFFFFF02,
    PollingTimeout = 0xFFFFFF03,
    PollingAgain = 0xFFFFFF04,
    FwTransfer = 0xFFFFFF05,
    FwEntryInvalid = 0xFFFFFF06,
    FlashProtect = 0xFFFFFF07,
}
