// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuRts54hidI2cSpeed {
    250K,
    400K,
    800K,
}

#[repr(u8)]
enum FuRts54hidCmd {
    ReadData        = 0xC0,
    WriteData       = 0x40,
}

#[repr(u8)]
enum FuRts54hidExt {
    Mcumodifyclock  = 0x06,
    ReadStatus      = 0x09,
    I2cWrite        = 0xC6,
    Writeflash      = 0xC8,
    I2cRead         = 0xD6,
    Readflash       = 0xD8,
    Verifyupdate    = 0xD9,
    Erasebank       = 0xE8,
    Reset2flash     = 0xE9,
}

#[repr(C, packed)]
#[derive(New)]
struct FuRts54HidCmdBuffer {
    cmd: FuRts54hidCmd,
    ext: FuRts54hidExt,
    dwregaddr: u32le,
    bufferlen: u16le,
    i2c_target_addr: u8,
    i2c_data_sz: u8,
    i2c_speed: FuRts54hidI2cSpeed,
    reserved: u8,
}
