// Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u32le)]
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
    Command9 = 0x06,
    Command63 = 0x07,
    ModeFlashProgram = 0x96,
    ReadBufferedResponse = 0xC7,
    GetDeviceStatus = 0xC9,
    SetDeviceMode = 0xCA,
    Reboot = 0xCE,
    SetFlashAddress = 0xD0,
    ReadFlash = 0xD1,
    EraseFlash = 0xD2,
    WriteFlash = 0xD3,
    ProtectFlash = 0xD4,
    CalculateFlashChecksum = 0xD5,
}

#[repr(u16le)]
enum FuWeidaRawCmd8760u16 {
    UnprotectLower508k = 0x0044,
    ProtectAll = 0x007C,
}

