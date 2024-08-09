// Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuWeidaRawDevType {
    FwNotSupport = 0x00,
    FwMaybeIsp = 0x01,
    Fw8755 = 0x02,
    Fw8760 = 0x04,
    Fw8762 = 0x08,
    Fw8790 = 0x10,
}

#[repr(u32le)]
#[derive(ToString)]
enum FuWeidaRawFirmwareFourcc {
    Riff = 0x46464952,
    Wif2 = 0x32464957,
    Whif = 0x46494857,
    Info = 0x4f464e49,
    Fsum = 0x4d555346,
    Fera = 0x41524546,
    Fbin = 0x4e494246,
    Frmt = 0x544D5246,
    Frwr = 0x52575246,
    Cnfg = 0x47464E43,
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

#[derive(New)]
struct FuWeidaRawCmdSetDeviceMode {
    cmd: FuWeidaRawCmd8760 == Command9,
    subcmd: FuWeidaRawCmd8760 == SetDeviceMode,
    mode: u8,
}

#[derive(New)]
struct FuWeidaRawCmdGetDeviceStatus {
    cmd: FuWeidaRawCmd8760 == Command9,
    subcmd: FuWeidaRawCmd8760 == GetDeviceStatus,
    offset: u8,
    bufsz: u8,
}

#[derive(New)]
struct FuWeidaRawCmdProtectFlash {
    cmd: FuWeidaRawCmd8760 == Command9,
    subcmd: FuWeidaRawCmd8760 == ProtectFlash,
    mask: u16le,
    mask_inv: u16le,
}

#[derive(New)]
struct FuWeidaRawCmdReboot {
    cmd: FuWeidaRawCmd8760 == Command9,
    subcmd: FuWeidaRawCmd8760 == Reboot,
    key0: u8 == 0xB9,
    key1: u8 == 0x0C,
    key2: u8 == 0x8A,
    key3: u8 == 0x24,
}

#[derive(New)]
struct FuWeidaRawCmdCalculateFlashChecksum {
    cmd: FuWeidaRawCmd8760 == Command9,
    subcmd: FuWeidaRawCmd8760 == CalculateFlashChecksum,
    flash_address: u32le,
    size: u32le,
    init_sum: u16le == 0x0,
}

#[derive(New)]
struct FuWeidaRawCmdReadBufferedResponse {
    cmd: FuWeidaRawCmd8760 == Command9,
    subcmd: FuWeidaRawCmd8760 == ReadBufferedResponse,
    size: u8,
}

#[derive(New)]
struct FuWeidaRawCmdSetFlashAddress {
    cmd: FuWeidaRawCmd8760 == Command9,
    subcmd: FuWeidaRawCmd8760 == SetFlashAddress,
    address: u32le,
}

#[derive(New)]
struct FuWeidaRawCmdEraseFlash {
    cmd: FuWeidaRawCmd8760 == Command9,
    subcmd: FuWeidaRawCmd8760 == EraseFlash,
    address_hi: u8,
    address_lo: u8,
}

#[derive(New)]
struct FuWeidaRawCmdWriteFlash {
    cmd: FuWeidaRawCmd8760 == Command63,
    subcmd: FuWeidaRawCmd8760 == WriteFlash,
    size: u8,
}

#[repr(u16le)]
enum FuWeidaRawCmd8760u16 {
    UnprotectLower508k = 0x0044,
    ProtectAll = 0x007C,
}

#[derive(ParseStream)]
struct FuWeidaRiffHeader {
    fourcc: FuWeidaRawFirmwareFourcc == Riff,
    file_size: u32,
    data_type: FuWeidaRawFirmwareFourcc == Whif,
}

#[derive(ParseStream)]
struct FuWeidaChunkHeader {
    fourcc: FuWeidaRawFirmwareFourcc == Frmt,
    size: u32, // of payload
}

#[derive(ParseStream)]
struct FuWeidaChunkWif {
    fourcc: FuWeidaRawFirmwareFourcc,
    size: u32,
    address: u32,
    spi_size: u32,
    reserved: [u32; 4],
}
