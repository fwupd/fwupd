// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum UsiDockSpiState {
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

enum UsiDockFirmwareIdx {
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

#[derive(New)]
struct UsiDockSetReportBuf {
    id: u8: const=2,
    length: u8,
    mcutag1: u8: const=0xFE,
    mcutag2: u8: const=0xFF,
    inbuf: [u8; 59],
    mcutag3: u8,
}

struct UsiDockIspVersion {
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
