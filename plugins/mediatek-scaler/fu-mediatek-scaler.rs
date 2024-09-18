/*
 * Copyright 2023 Dell Technologies
 * Copyright 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(u8)]
enum FuDdcOpcode {
    GetVcp = 0x01, // standard get vcp feature
    Req = 0xCC, // vendor specific opcode
}

#[repr(u8)]
enum FuDdcVcpCode {
    ControllerType = 0xC8, // standard display controller type
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
}

#[derive(New)]
struct FuStructDdcCmd {
    opcode: FuDdcOpcode = Req,
    vcp_code: FuDdcVcpCode,
}

#[repr(u8)]
enum FuDdcI2cAddr{
    DisplayDevice = 0x6E,
    HostDevice = 0x51,
    Checksum = 0x50,
}

#[derive(ToString)]
#[repr(u8)]
enum FuDdcciPriority{
    Normal,
    Up,
}

#[repr(u8)]
enum FuMediatekScalerIspStatus {
    Busy = 0x00,
    Failure,
    Success,
    Idle = 0x99,
}
