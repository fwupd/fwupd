// Copyright 2026 JS Park <mameforever2@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// SWIP Protocol Register Addresses
const FU_LXSTOUCH_REG_INFO_PANEL: u16 = 0x0110;
const FU_LXSTOUCH_REG_INFO_VERSION: u16 = 0x0120;
const FU_LXSTOUCH_REG_INFO_INTEGRITY: u16 = 0x0140;
const FU_LXSTOUCH_REG_INFO_INTERFACE: u16 = 0x0150;
const FU_LXSTOUCH_REG_CTRL_GETTER: u16 = 0x0600;
const FU_LXSTOUCH_REG_CTRL_SETTER: u16 = 0x0610;
const FU_LXSTOUCH_REG_CTRL_DFUP_FLAG: u16 = 0x0623;
const FU_LXSTOUCH_REG_FLASH_IAP_CTRL_CMD: u16 = 0x1400;
const FU_LXSTOUCH_REG_PARAMETER_BUFFER: u16 = 0x6000;

// Flash IAP Commands
#[repr(u8)]
enum FuLxsTouchCmd {
    FlashWrite = 0x03,
    Flash4kbUpdateMode = 0x04,
    FlashGetVerify = 0x05,
    WatchdogReset = 0x11,
}

// Protocol Modes
#[repr(u8)]
enum FuLxsTouchMode {
    Normal = 0x00,
    Diag = 0x01,
    Dfup = 0x02,
}

// Ready Status Values
#[repr(u8)]
enum FuLxsTouchReadyStatus {
    Ready = 0xA0,
    None = 0x05,
    Log = 0x77,
    Image = 0xAA,
}

// Write/Read Command Flags
#[repr(u8)]
enum FuLxsTouchFlag {
    Write = 0x68,
    Read = 0x69,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxsTouchPacket {
    report_id: u8,
    flag: FuLxsTouchFlag,
    length_lo: u8,
    length_hi: u8,
    command_hi: u8,
    command_lo: u8,
}

#[derive(New, Parse, Getters)]
#[repr(C, packed)]
struct FuStructLxsTouchInterface {
    protocol_name: [char; 8],
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxsTouchPanel {
    x_resolution: u16le,
    y_resolution: u16le,
    x_node: u8,
    y_node: u8,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxsTouchVersion {
    boot_ver: u16le,
    core_ver: u16le,
    app_ver: u16le,
    param_ver: u16le,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxsTouchCrc {
    boot_crc: u32le,
    app_crc: u32le,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxsTouchSetter {
    mode: FuLxsTouchMode,
    event_trigger_type: u8,
}

#[derive(New, Parse, Getters)]
#[repr(C, packed)]
struct FuStructLxsTouchGetter {
    ready_status: FuLxsTouchReadyStatus,
    event_ready: u8,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxsTouchFlashIapCmd {
    addr: u32le,
    size: u16le,
    status: u8,
    cmd: FuLxsTouchCmd,
}

