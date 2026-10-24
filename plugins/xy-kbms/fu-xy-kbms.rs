// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuXyKbmsReportId {
    Vendor = 0x02,
}

#[repr(u8)]
enum FuXyKbmsCmdKind {
    Data = 0x3E,
    Ctrl = 0x3F,
}

#[derive(ToString)]
#[repr(u8)]
enum FuXyKbmsDataCmd {
    ScanStart = 0x44,
    ScanStop = 0x45,
    Backlight = 0x66,
    Battery = 0x6A,
    Version = 0x6B,
    EnterBoot = 0xCC,
    EnterBootBt = 0xCD,
}

#[derive(ToString)]
#[repr(u8)]
enum FuXyKbmsCtrlCmd {
    WriteConfig = 0x11,
    ReadConfig = 0x22,
    WriteLayers = 0x33,
}

#[derive(ToString)]
#[repr(u8)]
enum FuXyKbmsBoardType {
    Debug = 0,
    TpWireless = 1,
    MechWireless = 2,
    TpMech = 3,
    Tp60Mech = 4,
    TpWireless2 = 5,
    Tp60Mech2 = 6,
    TpWireless3 = 7,
    WiredUsb = 10,
}

// from reverse engineering the vendor tool
#[derive(Bitfield, ToString)]
#[repr(u8)]
enum FuXyKbmsFlagsA {
    None = 0,
    TpMiddleScroll = 1 << 0,
    TpDoubleMiddleBksp = 1 << 1,
    MiddleBksp = 1 << 2,
    LowPowerKeepLed = 1 << 3,
    DisableWin = 1 << 4,
    TpOnAtPowerUp = 1 << 5,
    TpInvertXy = 1 << 6,
    UsbLowPower = 1 << 7,
}

// from reverse engineering the vendor tool
#[derive(Bitfield, ToString)]
#[repr(u8)]
enum FuXyKbmsFlagsB {
    None = 0,
    DisableStaBlink = 1 << 0,
    StaOff = 1 << 1,
    SwapFnLctrl = 1 << 2,
    BtPasskey = 1 << 3,
    SpecificWake = 1 << 4,
    Unknown5 = 1 << 5,
    Unknown6 = 1 << 6,
    Unknown7 = 1 << 7,
}

// special values and flags of the block index
#[repr(u16)]
enum FuXyKbmsBlockIdx {
    Mask = 0x3FFF,
    FlagFirmwareEnd = 0x8000,
    Ack = 0x2211,
    End = 0xFFFF,
}

// "02 3e <cmd> <data>" or "02 3f <cmd>"
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructXyKbmsCmdReq {
    report_id: FuXyKbmsReportId == Vendor,
    kind: FuXyKbmsCmdKind,
    cmd: u8,
    data: [u8; 16],
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructXyKbmsCmdRes {
    report_id: FuXyKbmsReportId == Vendor,
    kind: u8,
    cmd: u8,
    data: [u8; 16],
}

// the high bits of the index are flags, e.g. 0x8000 for the firmware end marker
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructXyKbmsBlockReq {
    report_id: FuXyKbmsReportId == Vendor,
    idx: u16le,
    data: [u8; 16],
}

// an index of 0x2211 is an ACK, anything else is the block the device wants next
#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructXyKbmsBlockRes {
    report_id: FuXyKbmsReportId == Vendor,
    idx: u16le,
    data: [u8; 8],
    _reserved: [u8; 8],
}

#[derive(New, Parse, Default)]
#[repr(C, packed)]
struct FuStructXyKbmsConfig {
    version: u8 = 0x04,
    board_type: FuXyKbmsBoardType,
    auto_off: u16le,
    tp_auto_off: u16le,
    _magic: u16le,
    scroll_speed: u8,
    move_speed: u8,
    flags_a: FuXyKbmsFlagsA,
    flags_b: FuXyKbmsFlagsB,
    bt_name: [char; 16],
    rf_mode: u8,
    rf_channel: u8,
    _reserved: [u8; 22],
    pinmap: [u8; 0x3C8],
    crc: u32le,
}
