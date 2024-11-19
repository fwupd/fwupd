// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuSynapticsMstFamily {
    Unknown = 0xFF,
    Tesla = 0,
    Leaf = 1,
    Panamera = 2,
    Cayenne = 3,
    Spyder = 4,
    Carrera = 5,
}

#[derive(ToString)]
enum FuSynapticsMstUpdcRc {
    Success,
    Invalid,
    Unsupported,
    Failed,
    Disabled,
    ConfigureSignFailed,
    FirmwareSignFailed,
    RollbackFailed,
}

#[derive(ToString)]
enum FuSynapticsMstUpdcCmd {
    EnableRc = 0x01,
    DisableRc = 0x02,
    GetId = 0x03,
    GetVersion = 0x04,
    FlashMapping = 0x07,
    EnableFlashChipErase = 0x08,
    CalEepromChecksum = 0x11,
    FlashErase = 0x14,
    CalEepromCheckCrc8 = 0x16,
    CalEepromCheckCrc16 = 0x17,
    ActivateFirmware = 0x18,
    WriteToEeprom = 0x20,
    WriteToMemory = 0x21,
    WriteToTxDpcd = 0x22, // TX0
    WriteToTxDpcdTx1 = 0x23,
    WriteToTxDpcdTx2 = 0x24,
    WriteToTxDpcdTx3 = 0x25,
    ReadFromEeprom = 0x30,
    ReadFromMemory = 0x31,
    ReadFromTxDpcd = 0x32, // TX0
    ReadFromTxDpcdTx1 = 0x33,
    ReadFromTxDpcdTx2 = 0x34,
    ReadFromTxDpcdTx3 = 0x35,
}

enum FuSynapticsMstRegRc {
    Cap    = 0x4B0,
    State  = 0x4B1,
    Cmd    = 0x4B2,
    Result = 0x4B3,
    Len    = 0x4B8,
    Offset = 0x4BC,
    Data   = 0x4C0,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructSynapticsFirmwareConfig {
    version: u8,
    reserved: u8,
    magic1: u8,
    magic2: u8,
}
