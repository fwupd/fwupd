// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructElantpFirmwareHdr {
    magic: [u8; 6] == 0xAA55CC33FFFF,
}

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructElantpHapticFirmwareHdr {
    magic: [u8; 4] == 0xFF40A25B,
}

enum FuEtpCmd {
    I2cEepromSettingInitial = 0x0000,
    GetHidDescriptor = 0x0001,
    GetHardwareId = 0x0100,
    I2cGetHidId = 0x0100,
    GetModuleId = 0x0101,
    I2cFwVersion = 0x0102,
    I2cOsmVersion = 0x0103,
    I2cForceTypeEnable = 0x0104,
    I2cIapIcbody = 0x0110,
    I2cIapVersion_2 = 0x0110,
    I2cIapVersion = 0x0111,
    I2cIapType = 0x0304,
    I2cFwPw = 0x030e,
    I2cFwChecksum = 0x030f,
    I2cIapCtrl = 0x0310,
    I2cIap = 0x0311,
    I2cIapReset = 0x0314,
    I2cIapChecksum = 0x0315,
    I2cSetEepromCtrl = 0x0321,
    I2cEepromSetting = 0x0322,
    ForceAddr = 0x03ad,
    I2cEepromWriteChecksum = 0x048B,
    I2cHapticRestart = 0x0601,
    I2cSetEepromLeaveIap = 0x0606,
    I2cSetEepromEnterIap = 0x0607,
    I2cCalcEepromChecksum = 0x060F,
    I2cSetEepromDatatype = 0x0702,
    I2cReadEepromChecksum = 0x070A,
    I2cGetEepromFwVersion = 0x0710,
    I2cGetEepromIapVersion = 0x0711,
    I2cEepromLongTransEnable = 0x4607,
    I2cEepromWriteInformation = 0x4600,
}
