// Copyright 2025 Harris Tai <harris_tai@pixart.com>
// Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

// ---- TP enums exported to C via rustgen ----

#[repr(u8)]
enum FuPxiTpResetMode {
    Application,
    Bootloader,
}

#[repr(u8)]
enum FuPxiTpResetKey1 {
    Suspend = 0xaa,
}

#[repr(u8)]
enum FuPxiTpResetKey2 {
    Regular    = 0xbb,
    Bootloader = 0xcc,
}

#[repr(u8)]
enum FuPxiTpFlashInst {
    Cmd0 = 0x00,
    Cmd1 = 0x01,
}

#[repr(u8)]
enum FuPxiTpFlashExecState {
    Busy    = 0x01,
    Success = 0x00,
}

#[repr(u8)]
enum FuPxiTpFlashWriteEnable {
    Success = 0x02,
}

#[repr(u8)]
enum FuPxiTpFlashStatus {
    Busy = 0x01,
}

#[repr(u32)]
enum FuPxiTpFlashCcr {
    WriteEnable = 0x0000_0106,
    ReadStatus  = 0x0100_0105,
    EraseSector = 0x0000_2520,
    ProgramPage = 0x0100_2502,
}

#[repr(u16)]
enum FuPxiTpPartId {
    Pjp274 = 0x0274,
}

#[repr(u8)]
enum FuPxiTpCrcCtrl {
    FwBank0    = 0x02,
    FwBank1    = 0x10,
    ParamBank0 = 0x04,
    ParamBank1 = 0x20,
    Busy       = 0x01,
}

// TP proxy mode (host<->TF pass-through)
#[repr(u8)]
enum FuPxiTpProxyMode {
    Normal   = 0x00,
    TfUpdate = 0x01,
}

// ---- TF enums exported to C via rustgen ----

// TF function command IDs (RMI addr)
#[repr(u16)]
enum FuPxiTfCmd {
    SetUpgradeMode   = 0x0001,
    WriteUpgradeData = 0x0002,
    ReadUpgradeStatus = 0x0003,
    ReadVersion      = 0x0007,
    TouchControl     = 0x0303,
}

// TF upgrade mode payload
#[repr(u8)]
enum FuPxiTfUpgradeMode {
    Exit      = 0x00,
    EnterBoot = 0x01,
    EraseFlash = 0x02,
}

// TF touch control payload
#[repr(u8)]
enum FuPxiTfTouchControl {
    Enable  = 0x00,
    Disable = 0x01,
}

// TF firmware version mode (mode: 1=APP, 2=BOOT, 3=ALGO)
#[repr(u8)]
enum FuPxiTfFwMode {
    App  = 1,
    Boot = 2,
    Algo = 3,
}

// TF frame constants (preamble, tail, flags)
#[repr(u8)]
enum FuPxiTfFrameConst {
    Preamble      = 0x5A,
    Tail          = 0xA5,
    ExceptionFlag = 0x80,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPxiTfWriteSimpleCmd {
    report_id:  u8 = 0xCC,
    preamble:   u8 = 0x5A,
    target_addr: u8 = 0x2C,
    func:       u8 = 0x00,
    addr:       u16le,
    len:        u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPxiTfWritePacketCmd {
    report_id:    u8 = 0xCC,
    preamble:     u8 = 0x5A,
    target_addr:   u8 = 0x2C,
    func:         u8 = 0x04,
    addr:         u16le,
    datalen:      u16le,
    packet_total: u16le,
    packet_index: u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPxiTfReadCmd {
    report_id:  u8 = 0xCC,
    preamble:   u8 = 0x5A,
    target_addr: u8 = 0x2C,
    func:       u8 = 0x0B,
    addr:       u16le,
    datalen:    u16le,
    reply_len:  u16le,
}
