// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuRts54hubRtd21xxIspStatus {
    Busy = 0xbb,        // host must wait for device
    IdleSuccess = 0x11, // previous command was OK
    IdleFailure = 0x12, // previous command failed
}

enum FuRts54hubI2cSpeed {
    100K,
    200K,
    300K,
    400K,
    500K,
    600K,
    700K,
    800K,
}

enum FuRts54hubRtd21xxVendorCmd {
    Disable,
    Enable,
    AccessFlash,
}

enum FuRts54hubRtd21xxBgIspCmd {
    None,
    FwUpdateStart,
    FwUpdateIspDone,
    GetFwInfo,
    FwUpdateExit,
    GetProjectIdAddr,
    SyncIdentifyCode,
}

enum FuRts54hubRtd21xxFgIspCmd {
    None,
    EnterFwUpdate,
    GetProjectIdAddr,
    SyncIdentifyCode,
    GetFwInfo,
    FwUpdateStart,
    FwUpdateIspDone,
    FwUpdateReset,
    FwUpdateExit,
}
