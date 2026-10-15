// Copyright 2026 Realtek Corporation
// Copyright 2026 Justin Li <justin_li@realsil.com.cn>
// SPDX-License-Identifier: LGPL-2.1-or-later

// 8-bit bus addresses; the kernel I2C_SLAVE ioctl wants them shifted right by one bit
#[repr(u8)]
enum FuRealtekEdpscalerTargetAddr {
    Ddcci = 0x6E, // DDCCI protocol target
    Fg = 0x3A,    // foreground ISP target
}

#[repr(u8)]
enum FuRealtekEdpscalerDdcciSubAddr {
    Status = 0x11,    // block-mode status readback
    BlockMode = 0x71, // block-mode command
}

#[repr(u8)]
enum FuRealtekEdpscalerDdcciOpcode {
    Vendor = 0x77, // DDCCI vendor opcode
}

#[repr(u8)]
enum FuRealtekEdpscalerDdcciCmd {
    BlockRw = 0xC8, // get/set block mode
}

#[repr(u8)]
enum FuRealtekEdpscalerDdcciFlag {
    LenMsb = 0x80, // set in the length byte of every DDCCI frame
}

// block-mode readback value; the same value is returned for enable and disable
#[repr(u8)]
enum FuRealtekEdpscalerBlockModeStatus {
    Ack = 0x00,
}

#[repr(u8)]
enum FuRealtekEdpscalerFgSubAddr {
    Status = 0x31,
    Cmd = 0x33,
    Data = 0x34,
}

// selector sent to the status register to change the update mode
#[repr(u8)]
enum FuRealtekEdpscalerFgSubOpcode {
    Foreground = 0x03,
}

#[repr(u8)]
enum FuRealtekEdpscalerFgOpcode {
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
enum FuRealtekEdpscalerStatus {
    IdleSuccess = 0x11,
    IdleFail = 0x12,
    Busy = 0xBB,
}
