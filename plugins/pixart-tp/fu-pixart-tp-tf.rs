// Copyright 2026 Harris Tai <harris_tai@pixart.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// host <-> tf pass-through proxy mode
#[repr(u8)]
enum FuPixartTpProxyMode {
    Normal   = 0x00,
    TfUpdate = 0x01,
}

// tf feature report / rmi frame constants
#[repr(u8)]
enum FuPixartTpTfFrameConst {
    Preamble      = 0x5a,
    Tail          = 0xa5,
    ExceptionFlag = 0x80,
}

// tf hid report ids
#[repr(u8)]
enum FuPixartTpTfReportId {
    PassThrough = 0xcc,
}

// tf rmi target address
#[repr(u8)]
enum FuPixartTpTfTargetAddr {
    RmiFrame = 0x2c,
}

// tf rmi function codes (func field)
#[repr(u8)]
enum FuPixartTpTfRmiFunc {
    WriteSimple = 0x00,
    WritePacket = 0x04,
    Read        = 0x0b,
}

// tf function command ids (high-level tf commands)
enum FuPixartTpTfCmd {
    SetUpgradeMode    = 0x0001,
    WriteUpgradeData  = 0x0002,
    ReadUpgradeStatus = 0x0003,
    ReadVersion       = 0x0007,
    TouchControl      = 0x0303,
}

// tf upgrade mode payload
enum FuPixartTpTfUpgradeMode {
    Exit       = 0x00,
    EnterBoot  = 0x01,
    EraseFlash = 0x02,
}

// tf touch control
enum FuPixartTpTfTouchControl {
    Enable  = 0x00,
    Disable = 0x01,
}

// tf firmware version mode
enum FuPixartTpTfFwMode {
    App  = 1,
    Boot = 2,
    Algo = 3,
}

// tf payload structs
#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPixartTpTfVersionPayload {
    major: u8,
    minor: u8,
    patch: u8,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPixartTpTfDownloadStatusPayload {
    status: u8,
    packet_number: u16le,
}

// tf command / reply structs

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpTfWriteSimpleCmd {
    report_id:   FuPixartTpTfReportId   = PassThrough,
    preamble:    FuPixartTpTfFrameConst = Preamble,
    target_addr: FuPixartTpTfTargetAddr = RmiFrame,
    func:        FuPixartTpTfRmiFunc    = WriteSimple,
    addr:        u16le,
    len:         u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpTfWritePacketCmd {
    report_id:    FuPixartTpTfReportId   = PassThrough,
    preamble:     FuPixartTpTfFrameConst = Preamble,
    target_addr:  FuPixartTpTfTargetAddr = RmiFrame,
    func:         FuPixartTpTfRmiFunc    = WritePacket,
    addr:         u16le,
    datalen:      u16le,
    packet_total: u16le,
    packet_index: u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpTfReadCmd {
    report_id:   FuPixartTpTfReportId   = PassThrough,
    preamble:    FuPixartTpTfFrameConst = Preamble,
    target_addr: FuPixartTpTfTargetAddr = RmiFrame,
    func:        FuPixartTpTfRmiFunc    = Read,
    addr:        u16le,
    datalen:     u16le,
    reply_len:   u16le,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPixartTpTfReplyHdr {
    report_id:   FuPixartTpTfReportId,
    preamble:    FuPixartTpTfFrameConst,
    target_addr: FuPixartTpTfTargetAddr,
    func:        FuPixartTpTfRmiFunc,
    datalen:     u16le,
}
