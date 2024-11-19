// Copyright 2024 Algoltek <Algoltek, Inc.>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuAlgoltekAuxCmd {
    Rdr = 0x06,
    Wrr,
    Rdv,
    En,
    Wrf = 0x10,
    Isp = 0x13,
    Ers = 0x19,
    Bot = 0x1D,
    Rst = 0x20,
    Vid = 0x22,
}

#[derive(ParseStream, ValidateStream, New, Default)]
#[repr(C, packed)]
struct FuStructAlgoltekAuxProductIdentity {
    header_len: u8 == 0x8,
    header: u64le == 0x4B45544C4F474C41, // 'A' 'L' 'G' 'O' 'L' 'T' 'E' 'K'
    product_name_len: u8,
    product_name: [char; 16],
    version_len: u8,
    version: [char; 48],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructAlgoltekAuxRdvCmdAddressPkt {
    i2c_address: u8 = 0x51,
    sublen: u8,
    address: u16be,
    len: u8,
    cmd: FuAlgoltekAuxCmd,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructAlgoltekAuxBotErsCmdAddressPkt {
    i2c_address: u8 = 0x51,
    sublen: u8,
    reserved: [u8; 2],
    len: u8,
    cmd: FuAlgoltekAuxCmd,
    address: u16be,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructAlgoltekAuxEnRstWrrCmdAddressPkt {
    i2c_address: u8 = 0x51,
    sublen: u8,
    reserved: [u8; 2],
    len: u8,
    cmd: FuAlgoltekAuxCmd,
    address: u16be,
    value: u16be,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructAlgoltekAuxIspFlashWriteCmdAddressPkt {
    i2c_address: u8 = 0x51,
    sublen: u8,
    serialno: u16be,
    len: u8,
    cmd: FuAlgoltekAuxCmd,
    data: [u8; 8],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructAlgoltekAuxCrcCmdAddressPkt {
    i2c_address: u8 = 0x51,
    sublen: u8,
    serialno: u16be,
    len: u8,
    cmd: FuAlgoltekAuxCmd,
    wcrc: u16be,
    reserved: [u8; 6],
}
