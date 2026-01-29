// Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// device register addresses
enum FuNvtTsMemMapReg {
    ChipVerTrimAddr         = 0x1fb104,
    SwrstSifAddr            = 0x1fb43e,
    EventBufCmdAddr         = 0x130950,
    EventBufHsSubCmdAddr    = 0x130951,
    EventBufResetStateAddr  = 0x130960,
    EventMapFwinfoAddr      = 0x130978,
    ReadFlashChecksumAddr   = 0x100000,
    RwFlashDataAddr         = 0x100002,
    EnbCascAddr             = 0x1fb12c,
    HidI2cEngAddr           = 0x1fb468,
    GcmCodeAddr             = 0x1fb540,
    GcmFlagAddr             = 0x1fb553,
    FlashCmdAddr            = 0x1fb543,
    FlashCmdIssueAddr       = 0x1fb54e,
    FlashCksumStatusAddr    = 0x1fb54f,
    BldSpePupsAddr          = 0x1fb535,
    QWrCmdAddr              = 0x000000,
}

enum FuNvtTsFlashMapConst {
    FlashNormalFwStartAddr  = 0x2000,
    FlashPidAddr            = 0x3f004,
    FlashFwSize             = 0x3c000,
}
