// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuSteelseriesSonicChip {
    Nordic,
    Holtek,
    Mouse,
}

#[derive(ToString)]
#[repr(u8)]
enum FuSteelseriesSonicWirelessStatus {
    Off,        // WDS not initiated, radio is off
    Idle,       // WDS initiated, USB receiver is transmitting beacon (mouse will not have this state)
    Search,     // WDS initiated, mouse is trying to synchronize to receiver (receiver will not have this state)
    Locked,     // USB receiver and mouse are synchronized, but not necessarily connected
    Connected,  // USB receiver and mouse are connected
    Terminated, // Mouse has been disconnected from the USB receiver
}

#[repr(u8)]
enum FuSteelseriesSonicOpcode8 {
    Battery = 0xAA,
    WirelessStatus = 0xE8,
}

#[derive(New, Default)]
struct FuStructSteelseriesSonicWirelessStatusReq {
    opcode: FuSteelseriesSonicOpcode8 == WirelessStatus,
}

#[derive(Parse)]
struct FuStructSteelseriesSonicWirelessStatusRes {
    status: FuSteelseriesSonicWirelessStatus,
}

#[derive(New, Default)]
struct FuStructSteelseriesSonicBatteryReq {
    opcode: FuSteelseriesSonicOpcode8 == Battery,
    bat_mode: u8 == 0x01, // percentage
}

#[derive(Parse)]
struct FuStructSteelseriesSonicBatteryRes {
    value: u16le,
}

#[derive(New)]
struct FuStructSteelseriesSonicRestartReq {
    opcode: u16le,
}

#[derive(New)]
struct FuStructSteelseriesSonicEraseReq {
    opcode: u16le,
    chipid: u16le,
}

#[derive(New)]
struct FuStructSteelseriesSonicReadFromRamReq {
    opcode: u16le,
    offset: u16le,
    size: u16le,
}

#[derive(Parse)]
struct FuStructSteelseriesSonicReadFromRamRes {
    data: [u8; 48],
}

#[derive(New)]
struct FuStructSteelseriesSonicReadFromFlashReq {
    opcode: u16le,
    chipid: u16le,
    offset: u32le,
    size: u16le,
}

#[derive(New)]
struct FuStructSteelseriesSonicWriteToRamReq {
    opcode: u16le,
    offset: u16le,
    size: u16le,
    data: [u8; 48],
}

#[derive(New)]
struct FuStructSteelseriesSonicWriteToFlashReq {
    opcode: u16le,
    chipid: u16le,
    offset: u32le,
    size: u16le,
}
