// Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuWeidaRawFirmwareFourcc {
    RIFF = 0x46464952,
    WIF2 = 0x32464957,
    WHIF = 0x46494857,
    INFO = 0x4f464e49,
    FSUM = 0x4d555346,
    FERA = 0x41524546,
    FBIN = 0x4e494246,
    FRMT = 0x544D5246,
    FRWR = 0x52575246,
    CNFG = 0x47464E43,
}

#[repr(u8)]
enum FuWeidaRawCmd8760 {
    COMMAND9 = 0x06,
    COMMAND63 = 0x07,
    MODE_FLASH_PROGRAM = 0x96,
    READ_BUFFERED_RESPONSE = 0xC7,
    GET_DEVICE_STATUS = 0xC9,
    SET_DEVICE_MODE = 0xCA,
    REBOOT = 0xCE,
    SET_FLASH_ADDRESS = 0xD0,
    READ_FLASH = 0xD1,
    RASE_FLASH = 0xD2,
    WRITE_FLASH = 0xD3,
    PROTECT_FLASH = 0xD4,
    CALCULATE_FLASH_CHECKSUM = 0xD5,
}

#[repr(u16)]
enum FuWeidaRawCmd8760u16 {
    UNPROTECT_LOWER508K = 0x0044,
    PROTECT_ALL = 0x007C,
}

