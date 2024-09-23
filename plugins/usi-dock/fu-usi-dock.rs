// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuUsiDockSpiState {
    None,
    SwitchSuccess,
    SwitchFail,
    CmdSuccess,
    CmdFail,
    RwSuccess,
    RwFail,
    Ready,
    Busy,
    Timeout,
    FlashFound,
    FlashNotFound,
}

#[derive(ToString)]
enum FuUsiDockFirmwareIdx {
    None  = 0x00,
    DmcPd = 0x01,
    Dp    = 0x02,
    Tbt4  = 0x04,
    Usb3  = 0x08,
    Usb2  = 0x10,
    Audio = 0x20,
    I225  = 0x40,
    Mcu   = 0x80,
}

#[repr(u8)]
enum FuUsiDockTag2 {
    IspBoot     = 0,    // before Common CMD for bootload, with TAG0, TAG1, CMD
    Isp         = 0x5A, // before Common, with TAG0, TAG1, CMD
    CmdMcu      = 0x6A, // USB->MCU(Common-cmd mode), with TAG0, TAG1, CMD
    CmdSpi      = 0x7A, // USB->MCU->SPI(Common-cmd mode), with TAG0, TAG1, CMD
    CmdI2c      = 0x8A, // USB->MCU->I2C(Mass data transmission)
    MassDataMcu = 0x6B, // MASS data transfer for MCU 0xA0
    MassDataSpi = 0x7B, // MASS data transfer for External flash 0xA1
    MassDataI2c = 0x8B, // MASS data transfer for TBT flash
}

#[repr(u8)]
enum FuUsiDockMcuCmd {
    McuNone = 0x0,
    McuStatus = 0x1,
    McuJump2boot = 0x2,
    ReadMcuVersionpage = 0x3,
    SetI225Pwr = 0x4,
    DockReset = 0x5,
    VersionWriteback = 0x6,
    SetChipType = 0x9,
    FwInitial = 0x0A,
    FwUpdate = 0x0B,
    FwTargetChecksum = 0x0C,
    FwIspEnd = 0x0D,
    All = 0xFF,
}

enum FuUsiDockSpiCmd {
    Initial = 0x01,
    EraseFlash = 0x02,
    Program = 0x03,
    WriteResponse = 0x04,
    ReadStatus = 0x05,
    Checksum = 0x06,
    End = 0x07,
    TransferFinish = 0x08,
    ErrorEnd = 0x09,
}

#[derive(New)]
struct FuStructUsiDockHidReq {
    id: u8 == 2,
    length: u8,
    buf: [u8; 61],
    tag3: FuUsiDockTag2,
}

#[derive(New)]
struct FuStructUsiDockMcuCmdReq {
    id: u8 == 2,
    length: u8,
    tag1: u8 == 0xFE,
    tag2: u8 == 0xFF,
    buf: [u8; 59],
    tag3: FuUsiDockTag2,
}

#[derive(Parse)]
struct FuStructUsiDockMcuCmdRes {
    id: u8 == 2,
    cmd1: FuUsiDockMcuCmd,
    tag1: u8 == 0xFE,
    tag2: u8 == 0xFF,
    cmd2: FuUsiDockMcuCmd,
    buf: [u8; 58],
    tag3: FuUsiDockTag2,
}

struct FuStructUsiDockIspVersion {
    dmc: [u8; 5],
    pd: [u8; 5],
    dp5x: [u8; 5],
    dp6x: [u8; 5],
    tbt4: [u8; 5],
    usb3: [u8; 5],
    usb2: [u8; 5],
    audio: [u8; 5],
    i255: [u8; 5],
    mcu: [u8; 2],
    bcdversion: [u8; 2],
}
