// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuPxiOtaSpecCheckResult {
    Ok = 1,
    FwOutOfBounds = 2,
    ProcessIllegal = 3,
    Reconnect = 4,
    FwImgVersionError = 5,
    DeviceLowBattery = 6,
}

#[derive(ToString)]
enum FuPxiWirelessModuleOtaRspCode {
    Ok,
    Finish,
    Fail,
    Error,
    WritePktLenError,
    WritePktTotalSizeError,
    ReadPktLenError,
    NotReady,
}

enum FuPxiOtaDisconnectReason {
    CodeJump = 1,
    UpdateDone = 2,
    Reset,
}

// OTA rsp code for wireless module
enum FuPxiWirelessModuleType {
    Mouse,
    Keyboard,
    Receiver,
}

enum FuPxiDeviceCmd {
    FwOtaInit		    = 0x10,
    FwWrite			    = 0x17,
    FwUpgrade		    = 0x18,
    FwMcuReset		    = 0x22,
    FwGetInfo		    = 0x23,
    FwObjectCreate	    = 0x25,
    FwOtaInitNew	    = 0x27,
    FwOtaRetransmit	    = 0x28,
    FwOtaDisconnect	    = 0x29,
    FwOtaGetNumOfModels = 0x2A,
    FwOtaGetModel	    = 0x2B,
    FwOtaPayloadContent = 0x40,
    FwOtaCheckCrc	    = 0x41,
    FwOtaInitNewCheck   = 0x42,
    FwOtaPreceding	    = 0x44,
}
