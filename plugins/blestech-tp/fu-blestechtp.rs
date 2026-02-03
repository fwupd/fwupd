/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

enum FuBlestechtpCmd {
    GetChecksum = 0x3f,
    GetFwVer = 0xb6,
    UpdateStart = 0xb1,
    ProgramPage = 0xb2,
    ProgramPageEnd = 0xb3,
    ProgramChecksum = 0xb4,
    ProgramEnd = 0xb5,
}