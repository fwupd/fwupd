// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Parse, ParseStream)]
#[repr(C, packed)]
struct FuStructVliPdHdr {
    fwver: u32be,
    vid: u16le,
    pid: u16le,
}

enum FuVliPdRegisterAddress {
    GpioControlA = 0x0003,
    ProjLegacy   = 0x0018,
    ProjIdHigh   = 0x009C,
    ProjIdLow    = 0x009D,
}

enum FuVliPdDeviceSpiCmd {
    ReadStatus  = 0xC2,
    ReadData    = 0xC4,
    ChipErase   = 0xD1,
    SectorErase = 0xD2,
    WriteEnable = 0xD4,
    WriteStatus = 0xD8,
    WriteData   = 0xDC,
}

enum FuVliPdDeviceCmd {
    AccessRegister = 0xE0,
    GetFwVersion   = 0xE2,
}

#[derive(New, Parse, ParseStream, ToString)]
#[repr(C, packed)]
struct FuStructVliUsbhubHdr {
    dev_id: u16be,
    strapping1: u8,
    strapping2: u8,
    usb3_fw_addr: u16be,
    usb3_fw_sz: u16be,
    usb2_fw_addr: u16be,
    usb2_fw_sz: u16be,
    usb3_fw_addr_high: u8,
    usb3_fw_sz_high: u8,
    _unknown_0e: [u8; 2],
    usb2_fw_addr_high: u8,
    _unknown_11: [u8; 10],
    inverse_pe41: u8,
    prev_ptr: u8,        // addr / 0x20
    next_ptr: u8,        // addr / 0x20
    variant: u8,
    checksum: u8,
}

#[derive(ToString, FromString)]
enum FuVliDeviceKind {
    Unknown = 0x0,
    Vl100 = 0x0100,
    Vl101 = 0x0101,
    Vl102 = 0x0102,
    Vl103 = 0x0103,
    Vl104 = 0x0104,
    Vl105 = 0x0105,
    Vl106 = 0x0106,
    Vl107 = 0x0107,
    Vl108 = 0x0108,
    Vl109 = 0x0109,
    Vl120 = 0x0120,
    Vl122 = 0x0122,
    Vl210 = 0x0210,
    Vl211 = 0x0211,
    Vl212 = 0x0212,
    Vl650 = 0x0650,
    Vl810 = 0x0810,
    Vl811 = 0x0811,
    Vl811pb0 = 0x8110,
    Vl811pb3 = 0x8113,
    Vl812b0 = 0xa812,
    Vl812b3 = 0xb812,
    Vl812q4s = 0xc812,
    Vl813 = 0x0813,
    Vl815 = 0x0815,
    Vl817 = 0x0817,
    Vl817s = 0xa817,
    Vl819q7 = 0xa819, // guessed
    Vl819q8 = 0xb819, // guessed
    Vl820q7 = 0xa820,
    Vl820q8 = 0xb820,
    Vl821q7 = 0xa821, // guessed
    Vl821q8 = 0xb821, // guessed
    Vl822q5 = 0x0822, // guessed
    Vl822q7 = 0xa822, // guessed
    Vl822q8 = 0xb822, // guessed
    Vl822t = 0xc822, // guessed
    Vl822c0 = 0xd822,
    Vl830 = 0x0830,
    Vl832 = 0x0832,
    Msp430 = 0xf430,  // guessed
    Ps186 = 0xf186,   // guessed
    Rtd21xx = 0xff00, // guessed
}
