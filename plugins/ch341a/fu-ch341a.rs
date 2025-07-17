// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuCh341aCmd {
    SetOutput = 0xA1,
    IoAddr = 0xA2,
    PrintOut = 0xA3,
    SpiStream = 0xA8,
    SioStream = 0xA9,
    I2cStream = 0xAA,
    UioStream = 0xAB,
}

enum FuCh341aCmdI2c {
    StmStart = 0x74,
    StmStop = 0x75,
    StmOut = 0x80,
    StmIn = 0xC0,
    StmSet = 0x60,
    StmUs = 0x40,
    StmMs = 0x50,
    StmDly = 0x0F,
    StmEnd = 0x00,
}

enum FuCh341aCmdUio {
    StmIn = 0x00,
    StmDir = 0x40,
    StmOut = 0x80,
    StmUs = 0xC0,
    StmEnd = 0x20,
}

enum FuCh341aStmI2cSpeed {
    Low = 0x00,
    Standard = 0x01,
    Fast = 0x02,
    High = 0x03,
}
