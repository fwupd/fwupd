// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuAnalogixUpdateStatus {
    Invalid,
    Start,
    Finish,
    Error = 0xFF,
}

// bRequest for Phoenix-Lite Billboard
enum FuAnalogixBbRqt {
    SendUpdateData = 0x01,
    ReadUpdateData = 0x02,
    GetUpdateStatus = 0x10,
    ReadFwVer = 0x12,
    ReadCusVer = 0x13,
    ReadFwRver = 0x19,
    ReadCusRver = 0x1C,
}

// wValue low byte
enum FuAnalogixBbWval {
    UpdateOcm = 0x06,
    UpdateCustomDef = 0x07,
    UpdateSecureTx = 0x08,
    UpdateSecureRx = 0x09,
}
