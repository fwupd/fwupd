// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructSynapticsVmm9 {
    signature: [char; 7] == "CARRERA",
}

enum FuSynapticsVmm9CommandFlags {
    None = 0,
    FullBuffer = 1 << 0,
    NoReply = 1 << 1,
    IgnoreReply = 1 << 2,
}

#[repr(u8)]
enum FuSynapticsVmm9RcCtrl {
    EnableRc = 0x01,
    DisableRc = 0x02,
    GetId = 0x03,
    EraseFlash = 0x14,
    ActivateFirmware = 0x18,
    WriteFlashData = 0x20,
    MemoryWrite = 0x21,
    TxDpcdRegisterWrite = 0x22, // TX0 to TX3
    ReadFlashData = 0x30,
    MemoryRead = 0x31,
    TxDpcdRegisterRead = 0x32, // TX0 to TX3

    // these are fake, but useful for debugging
    EnableRcBusy = 0x80|0x01,
    DisableRcBusy = 0x80|0x02,
    GetIdBusy = 0x80|0x03,
    EraseFlashBusy = 0x80|0x14,
    ActivateFirmwareBusy = 0x80|0x18,
    WriteFlashDataBusy = 0x80|0x20,
    MemoryWriteBusy = 0x80|0x21,
    ReadFlashDataBusy = 0x80|0x30,
    MemoryReadBusy = 0x80|0x31,
}

enum FuSynapticsVmm9MemOffset {
    ChipSerial          = 0x20200D3C, // 0x4 bytes, %02x
    RcTrigger           = 0x2020A024, // write 0xF5000000 to reset
    McuBootloaderSts    = 0x2020A030, // bootloader status
    McuFwVersion        = 0x2020A038, // 0x4 bytes, maj.min.mic.?
    FirmwareBuild       = 0x2020A084, // 0x4 bytes, be
    RcCommand           = 0x2020B000,
    RcOffset            = 0x2020B004,
    RcLength            = 0x2020B008,
    RcData              = 0x2020B010, // until 0x2020B02C
    FirmwareName        = 0x90000230, // 0xF bytes, ASCII
    BoardId             = 0x9000014E, // 0x2 bytes, customer.hardware
}

#[derive(ToString)]
#[repr(u8)]
enum FuSynapticsVmm9RcSts {
    Success,
    Invalid,
    Unsupported,
    Failed,
    Disabled,
    ConfigureSignFailed,
    FirmwareSignFailed,
    RollbackFailed,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructHidPayload {
    cap: u8,
    state: u8,
    ctrl: FuSynapticsVmm9RcCtrl,
    sts: FuSynapticsVmm9RcSts,
    offset: u32le,
    length: u32le,
    fifo: [u8; 32],
}

#[derive(New, ToString, Getters, Default)]
#[repr(C, packed)]
struct FuStructHidSetCommand {
    id: u8 == 0x1,
    type: u8 == 0x0, // packet write
    size: u8,
    payload: FuStructHidPayload,
    checksum: u8, // this is actually lower if @rc_fifo is less than 32 bytes
}

#[derive(New, Parse, Default)]
#[repr(C, packed)]
struct FuStructHidGetCommand {
    id: u8 == 0x1,
    type: u8 == 0x0, // packet reply
    size: u8,
    payload: FuStructHidPayload,
    checksum: u8, // payload is always 32 bytes
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructSynapticsUpdGetId {
    _pid: u16le,
    cid: u8,
    bid: u8,
}
