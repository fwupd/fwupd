// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuQsiDockFirmwareIdx {
    None = 0,
    DmcPd = 1 << 0,
    Dp = 1 << 1,
    Tbt4 = 1 << 2,
    Usb3 = 1 << 3,
    Usb2 = 1 << 4,
    Audio = 1 << 5,
    I225 = 1 << 6,
    Mcu = 1 << 7,
}

enum FuQsiDockCmd1 {
    Boot = 0x11,
    System = 0x31,
    Mcu = 0x51,
    Spi = 0x61,
    I2cVmm = 0x71,
    I2cCcg = 0x81,

    MassMcu = 0xC0,
    MassSpi,
    MassI2cVmm,
    MassI2cCy,
}

enum FuQsiDockCmd2Cmd {
    DeviceStatus,
    SetBootMode,
    SetApMode,
    EraseApPage,
    Checksum,
    DeviceVersion,
    DevicePcbVersion,
    DeviceSn,
}

enum FuQsiDockCmd2Spi {
    ExternalFlashIni,
    ExternalFlashErase,
    ExternalFlashChecksum,
}

#[repr(C, packed)]
struct FuQsiDockIspVersionInMcu {
    dmc: [u8; 5],
    pd: [u8; 5],
    dp5x: [u8; 5],
    dp6x: [u8; 5],
    tbt4: [u8; 5],
    usb3: [u8; 5],
    usb2: [u8; 5],
    audio: [u8; 5],
    i225: [u8; 5],
    mcu: [u8; 2],
    bcd_version: [u8; 2],
}
