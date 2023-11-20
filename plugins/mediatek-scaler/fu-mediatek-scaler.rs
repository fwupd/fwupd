/*
 * Copyright (C) 2023 Dell Technologies
 * Copyright (C) 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#[repr(u8)]
enum DdcOpcode {
    Req = 0xCC, // vendor specific opcode
    GetVcp = 0x01, // standard get vcp feature
}

#[repr(u8)]
enum DdcVcpCode {
    Priority = 0x90,
    UpdatePrep = 0xF2,
    UpdateAck = 0xF3,
    SetData = 0xF4,
    GetStaged = 0xF5,
    SetDataFf = 0xF6,
    CommitFw = 0xF7,
    GetIspMode = 0xF8,
    Reboot = 0xFB,
    Sum = 0xFE,
    Version = 0xFF,
    ControllerType = 0xC8, // standard display controller type
}

#[derive(New)]
struct DdcCmd {
    opcode: DdcOpcode = Req,
    vcp_code: DdcVcpCode,
}

#[repr(u8)]
enum DdcI2cAddr{
    DisplayDevice = 0x6E,
    HostDevice = 0x51,
    Checksum = 0x50,
}

#[repr(u8)]
enum DdcciPriority{
    Normal,
    Up,
}
