// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum PxiOtaSpecCheckResult {
    Ok = 1,
    FwOutOfBounds = 2,
    ProcessIllegal = 3,
    Reconnect = 4,
    FwImgVersionError = 5,
    DeviceLowBattery = 6,
}

#[derive(ToString)]
enum PxiWirelessModuleOtaRspCode {
    Ok,
    Finish,
    Fail,
    Error,
    WritePktLenError,
    WritePktTotalSizeError,
    ReadPktLenError,
    NotReady,
}
