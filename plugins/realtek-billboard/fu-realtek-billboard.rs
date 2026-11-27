// Copyright 2026 Realtek Corporation
// Copyright 2026 Shadow Zhang <shadow_zhang@realsil.com.cn>
// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// bRequest codes for the LVFS-protocol commands, the opcode is carried in the
// low byte of wValue and every reply starts with a one byte bRequest echo
#[repr(u8)]
enum FuRealtekBillboardCmd {
    GetFwVersion = 0x20, // no opcode, reply = 0x20 + 2 byte version
    ProjectId = 0x21,    // opcode in FuRealtekBillboardProjectIdOp
    FwUpdate = 0x22,     // opcode in FuRealtekBillboardFwUpdateOp
    Reset = 0x23,        // no data stage, STALL means failure
}

// opcodes for FU_REALTEK_BILLBOARD_CMD_PROJECT_ID, used as the low byte of wValue
#[repr(u8)]
enum FuRealtekBillboardProjectIdOp {
    GetAddressInfo = 0x01,  // reply = 0x21 + 0x01 + 4 byte address + 1 byte length
    SetVerify = 0x02,       // host sends the Project ID to verify
    GetVerifyResult = 0x03, // reply = 0x21 + 0x03 + 1 byte result
}

// opcodes for FU_REALTEK_BILLBOARD_CMD_FW_UPDATE, used as the low byte of wValue
#[repr(u8)]
enum FuRealtekBillboardFwUpdateOp {
    Start = 0x01,     // reply = 0x22 + 0x01 + 1 byte result
    WriteData = 0x02, // host sends firmware data
    Finish = 0x03,    // reply = 0x22 + 0x03 + 1 byte result
}

// result byte of the Project ID verify and firmware update commands
#[repr(u8)]
enum FuRealtekBillboardCmdResult {
    Fail = 0x00,
    Success = 0x01,
}

// GetFwVersion reply: the command echo followed by the two version bytes
#[derive(Parse, ToString)]
#[repr(C, packed)]
struct FuStructRealtekBillboardFwVersionRsp {
    cmd: FuRealtekBillboardCmd,
    major: u8,
    minor: u8,
}

// GetAddressInfo reply: the command and opcode echoes, then the offset of the
// Project ID within the image and its length in bytes
#[derive(Parse, ToString)]
#[repr(C, packed)]
struct FuStructRealtekBillboardProjectIdAddressInfoRsp {
    cmd: FuRealtekBillboardCmd,
    opcode: FuRealtekBillboardProjectIdOp,
    address: u32be,
    length: u8,
}

// reply shared by the commands that only report success or failure
#[derive(Parse, ToString, Default)]
#[repr(C, packed)]
struct FuStructRealtekBillboardResultRsp {
    cmd: FuRealtekBillboardCmd,
    opcode: u8,
    result: FuRealtekBillboardCmdResult == Success,
}
