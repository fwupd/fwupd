/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(u8)]
enum EcCmd {
    SetDockPkg = 0x01,
    GetDockInfo = 0x02,
    GetDockData = 0x03,
    GetDockType = 0x05,
    SetModifyLock = 0x0a,
    SetDockReset = 0x0b,
    SetPassive = 0x0d,
    GetFwUpdateStatus = 0x0f,
}
