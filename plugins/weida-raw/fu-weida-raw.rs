// Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
// SPDX-License-Identifier: LGPL-2.1-or-later


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

