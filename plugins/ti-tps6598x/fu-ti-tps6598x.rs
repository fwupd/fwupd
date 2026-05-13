// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructTiTps6598xFirmwareHdr {
    magic: u32le == 0xACEF0001,
}

#[derive(ToString)]
enum FuTiTps6598xSfwi {
    Success = 0x0,
    FailFlashErrorOrBusy = 0x4,
    FailFlashInvalidAddress = 0x5,
    FailLastBootWasUart = 0x6,
    FailSfwiAfterComplete = 0x7,
    FailNoValidFlashRegion = 0x8,
    FailUnknownError = 0xF,
}

#[derive(ToString)]
enum FuTiTps6598xSfwd {
    Success = 0x0,
    FailFlashEraseWriteError = 0x4,
    FailSfwiNotRunFirst = 0x6,
    FailTooMuchData = 0x7,
    FailIdNotInHeader = 0x8,
    FailBinaryTooLarge = 0x9,
    FailDeviceIdMismatch = 0xA,
    FailFlashErrorReadOnly = 0xD,
    FailUnknownError = 0xF,
}

#[derive(ToString)]
enum FuTiTps6598xSfws {
    Success = 0x0,
    FailFlashEraseWriteError = 0x4,
    FailSfwdNotRunOrNoKeyExists = 0x6,
    FailTooMuchData = 0x7,
    FailCrcFail = 0x8,
    FailDidCheckFail = 0x9,
    FailVersionCheckFail = 0xA,
    FailNoHashMatchRuleSatisfied = 0xB,
    FailEngrFwUpdateAttemptWhileRunningProd = 0xC,
    FailIncompatibleRomVersion = 0xD,
    FailCrcBusy = 0xE,
    FailUnknownError = 0xF,
}

enum FuTiTps6598xRegister {
    TbtVid = 0x00, // ro, 4 bytes -- Intel assigned
    TbtDid = 0x01, // ro, 4 bytes -- Intel assigned
    ProtoVer = 0x02, // ro, 4 bytes
    Mode = 0x03, // ro, 4 bytes
    Type = 0x04, // ro, 4 bytes
    Uid = 0x05, // ro, 16 bytes
    Ouid = 0x06, // ro, 8 bytes
    Cmd1 = 0x08, // ro, 4CC
    Data1 = 0x09, // rw, 64 bytes
    Version = 0x0F, // rw, 4 bytes
    Cmd2 = 0x10, // ro, 4CC
    Data2 = 0x11, // rw, 64 bytes
    Cmd3 = 0x1E, // ro, variable
    Data3 = 0x1F, // ro, variable
    Otp_config = 0x2D, // ro, 12 bytes
    BuildIdentifier = 0x2E, // ro, 64 bytes
    DeviceInfo = 0x2F, // ro, 47 bytes
    TxIdentity = 0x47, // rw, 49 bytes
}
