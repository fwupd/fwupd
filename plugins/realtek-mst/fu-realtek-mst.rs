// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuRealtekMstDeviceDualBankMode {
    UserOnly,
    Diff,
    Copy,
    UserOnlyFlag,
}

#[derive(ToString)]
enum FuRealtekMstDeviceFlashBank {
    Boot,
    User1,
    User2,
}

enum FuRealtekMstReg {
    CmdAttr = 0x60,
    EraseOpcode = 0x61,
    CmdAddrHi = 0x64,   // for commands
    CmdAddrMid = 0x65,
    CmdAddrLo = 0x66,
    ReadOpcode = 0x6A,
    WriteOpcode = 0x6D,
    McuMode = 0x6F,     // mode register address
    WriteFifo = 0x70,   // write data into write buffer
    WriteLen = 0x71,    // number of bytes to write minus 1
    IndirectLo = 0xF4,
    IndirectHi = 0xF5,
}
