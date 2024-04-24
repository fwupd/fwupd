// Copyright (C) 2024 Algoltek <Algoltek, Inc.>
// SPDX-License-Identifier: LGPL-2.1+

enum AlgoltekAuxCmd {
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
    CrcInitPolinom = 0x1021,
    CrcPolinom = 0x1021,
}

#[derive(ParseStream, ValidateStream)]
struct AlgoltekAuxProductIdentity {
    header_len: u8,
    header: u64le == 0x4B45544C4F474C41, // 'A' 'L' 'G' 'O' 'L' 'T' 'E' 'K'
    product_name_len: u8,
    product_name: [char; 16],
    version_len: u8,
    version: [char; 48],
}

#[derive(New)] //6
struct AlgoltekAuxRdvCmdAddressPkt {
    i2cAddress: u8,
    sublen: u8,
    address: u16be,
    len: u8,
    cmd: u8,
}

#[derive(New)] //8
struct AlgoltekAuxBotErsCmdAddressPkt {
    i2cAddress: u8,
    sublen: u8,
    reserved: [u8; 2],
    len: u8,
    cmd: u8,
    address: u16be,
}

#[derive(New)] //10
struct AlgoltekAuxEnRstWrrCmdAddressPkt {
    i2cAddress: u8,
    sublen: u8,
    reserved: [u8; 2],
    len: u8,
    cmd: u8,
    address: u16be,
    value: u16be,
}

#[derive(New)] //14-1
struct AlgoltekAuxIspFlashWriteCmdAddressPkt {
    i2cAddress: u8,
    sublen: u8,
    serialno: u16be,
    len: u8,
    cmd: u8,
    data: [u8; 8],
}

#[derive(New)] //14-2 crc
struct AlgoltekAuxCrcCmdAddressPkt {
    i2cAddress: u8,
    sublen: u8,
    serialno: u16be,
    len: u8,
    cmd: u8,
    wcrc: u16be,
    reserved: [u8; 6],
}



