// Copyright 2026 JS Park <mameforever2@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Device IDs
const FU_LXSTOUCH_VID_NORMAL: u16 = 0x1FD2;
const FU_LXSTOUCH_PID_NORMAL_B011: u16 = 0xB011;
const FU_LXSTOUCH_VID_DFUP: u16 = 0x29BD;
const FU_LXSTOUCH_PID_DFUP: u16 = 0x5357;

// Communication Constants
const FU_LXSTOUCH_BUFFER_SIZE: u32 = 64;
const FU_LXSTOUCH_REPORT_ID: u8 = 0x09;

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
const FU_LXSTOUCH_CMD_FLASH_WRITE: u8 = 0x03;
const FU_LXSTOUCH_CMD_FLASH_4KB_UPDATE_MODE: u8 = 0x04;
const FU_LXSTOUCH_CMD_FLASH_GET_VERIFY: u8 = 0x05;
const FU_LXSTOUCH_CMD_WATCHDOG_RESET: u8 = 0x11;

// Protocol Modes
const FU_LXSTOUCH_MODE_NORMAL: u8 = 0x00;
const FU_LXSTOUCH_MODE_DIAG: u8 = 0x01;
const FU_LXSTOUCH_MODE_DFUP: u8 = 0x02;

// Ready Status Values
const FU_LXSTOUCH_READY_STATUS_READY: u8 = 0xA0;
const FU_LXSTOUCH_READY_STATUS_NONE: u8 = 0x05;
const FU_LXSTOUCH_READY_STATUS_LOG: u8 = 0x77;
const FU_LXSTOUCH_READY_STATUS_IMAGE: u8 = 0xAA;

// Write/Read Command Flags
const FU_LXSTOUCH_FLAG_WRITE: u8 = 0x68;
const FU_LXSTOUCH_FLAG_READ: u8 = 0x69;

// Firmware Sizes
const FU_LXSTOUCH_FW_SIZE_APP_ONLY: u32 = 112 * 1024; // 0x1C000
const FU_LXSTOUCH_FW_SIZE_BOOT_APP: u32 = 128 * 1024; // 0x20000
const FU_LXSTOUCH_FW_OFFSET_APP_ONLY: u16 = 0x4000;

// Download Configuration
const FU_LXSTOUCH_DOWNLOAD_CHUNK_SIZE_NORMAL: u32 = 128;
const FU_LXSTOUCH_DOWNLOAD_CHUNK_SIZE_4K: u32 = 4096;
const FU_LXSTOUCH_TRANSMIT_UNIT_NORMAL: u32 = 16;
const FU_LXSTOUCH_TRANSMIT_UNIT_4K: u32 = 48;

// Timeouts
const FU_LXSTOUCH_TIMEOUT_READY_MS: u32 = 5000;
const FU_LXSTOUCH_TIMEOUT_RECONNECT_MS: u32 = 5000;

// ChromeOS Fast Version Check Requirement
const FU_LXSTOUCH_VERSION_FAST_MAX_MS: u32 = 40;

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchPacket {
    report_id: u8,
    flag: u8,
    length_lo: u8,
    length_hi: u8,
    command_hi: u8,
    command_lo: u8,
    // data: [u8; N], // 실제 데이터는 C에서 memcpy로 처리
}

#[derive(New, Parse, Getters)]
#[repr(C, packed)]
struct FuStructLxstouchInterface {
    protocol_name: [char; 8],
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchPanel {
    x_resolution: u16le,
    y_resolution: u16le,
    x_node: u8,
    y_node: u8,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchVersion {
    boot_ver: u16le,
    core_ver: u16le,
    app_ver: u16le,
    param_ver: u16le,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchCrc {
    boot_crc: u32le,
    app_crc: u32le,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchSetter {
    mode: u8,
    event_trigger_type: u8,
}

#[derive(New, Parse, Getters)]
#[repr(C, packed)]
struct FuStructLxstouchGetter {
    ready_status: u8,
    event_ready: u8,
}

#[derive(New, Parse, Getters, Setters)]
#[repr(C, packed)]
struct FuStructLxstouchFlashIapCmd {
    addr: u32le,
    size: u16le,
    status: u8,
    cmd: u8,
}

