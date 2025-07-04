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
