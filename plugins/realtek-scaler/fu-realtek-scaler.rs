// Copyright 2026 Realtek Corporation
// Copyright 2026 Justin Li <justin_li@realsil.com.cn>
// SPDX-License-Identifier: LGPL-2.1-or-later

// 8-bit bus addresses; the kernel I2C_SLAVE ioctl wants them shifted right by one bit
#[repr(u8)]
enum FuRealtekScalerTargetAddr {
    Ddcci = 0x6E, // DDCCI protocol target
    Fg = 0x3A,    // foreground ISP target
}

#[repr(u8)]
enum FuRealtekScalerDdcciSubAddr {
    Cmd = 0x71, // DDCCI vendor command
}

#[repr(u8)]
enum FuRealtekScalerDdcciOpcode {
    Vendor = 0x77, // DDCCI vendor opcode
}

#[repr(u8)]
enum FuRealtekScalerDdcciFlag {
    LenMsb = 0x80, // set in the length byte of every DDCCI frame
}

#[repr(u8)]
enum FuRealtekScalerFgSubAddr {
    Status = 0x31,
    Cmd = 0x33,
    Data = 0x34,
}

// selector sent to the status register to change the update mode
#[repr(u8)]
enum FuRealtekScalerFgSubOpcode {
    Foreground = 0x03,
}

#[repr(u8)]
enum FuRealtekScalerFgOpcode {
    EnterHighPri = 0x01,
    GetProjectIdAddr = 0x02,
    VerifyProjectId = 0x03,
    GetFwInfo = 0x04,
    Start = 0x05,
    Finish = 0x06,
    FwUpdateReset = 0x07, // reset into the new firmware
    FwUpdateExit = 0x08,  // leave foreground mode without a reset
}

#[derive(ToString)]
#[repr(u8)]
enum FuRealtekScalerStatus {
    IdleSuccess = 0x11,
    IdleFail = 0x12,
    Busy = 0xBB,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructRealtekScalerFwInfo {
    _unknown: u8,
    major: u8,
    minor: u8,
    _reserved: [u8; 4],
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructRealtekScalerProjectIdAddr {
    status: FuRealtekScalerStatus,
    addr: u32be,
    count: u8,
}
