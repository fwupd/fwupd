// Copyright 2024 FocalTech Systems Co., Ltd.
// SPDX-License-Identifier: LGPL-2.1-or-later

// FocalTech MOC protocol — derived from UpgradeTool/ff_update.c
//
// Standard command packet (ff_cmd_buf_t):
//   [ 0x02 | LEN_HI | LEN_LO | CMD | data... | BCC ]
//   - LEN (big-endian): len(data) + 1  (the +1 counts the BCC byte)
//   - BCC: XOR of all bytes from LEN_HI through the last data byte
//
// Firmware-download packet (ff_down_buf_t) — used only with CMD_FW_DOWNLOAD:
//   [ 0x02 | LEN_HI | LEN_LO | CMD(0x33) | MAGIC | SEQ | data(1024) | BCC ]
//   - MAGIC: FuFocalMocMagic (Sho / Stx / Eot)
//   - SEQ:   monotonically-incrementing packet counter (uint8)
//
// All device responses are in standard ff_cmd_buf_t format with CMD = 0x04 (ACK).
// There is NO status byte inside ACK data — success is indicated solely by CMD == 0x04.

// Command codes (ff_update.c)
#[repr(u8)]
enum FuFocalMocCmd {
    GetFwVersion = 0x30,  // request firmware version string
    SetBootMode  = 0x32,  // switch device mode; data[0] = FuFocalMocBootMode
    FwDownload   = 0x33,  // firmware download (3-phase: SHO → STX×N → EOT)
    Ack          = 0x04,  // device acknowledgment (response to all commands)
}

// Boot mode argument for CMD_SET_BOOT_MODE (ff_update.h: boot_mode_t)
#[repr(u8)]
enum FuFocalMocBootMode {
    EnterApp     = 0x00,  // return to normal application mode
    EnterBoot    = 0x01,  // enter bootloader (IAP) mode for firmware update
    EnterRomBoot = 0x02,  // enter ROM bootloader mode
}

// Magic field in the firmware-download packet (ff_update.c: SHO/STX/EOT)
#[repr(u8)]
enum FuFocalMocMagic {
    Sho = 0x01,  // Start-of-Header: first packet, carries filename + size + CRC32
    Stx = 0x02,  // Start-of-Text: middle data block (1024 bytes each)
    Eot = 0x04,  // End-of-Text: last (possibly partial) data block
}

// Standard command request header (ff_cmd_buf_t without the flex data[] member)
#[derive(New)]
#[repr(C, packed)]
struct FuStructFocalMocCmdReq {
    head: u8,              // MSG_MAGIC = 0x02
    ln: u16be,             // payload length (data bytes + 1 for BCC), big-endian
    cmd: FuFocalMocCmd,
}

// Device response header (same wire layout as the request)
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructFocalMocCmdRsp {
    head: u8,
    ln: u16be,
    cmd: FuFocalMocCmd,  // always FU_FOCAL_MOC_CMD_ACK (0x04) for success
}

// Firmware-download packet header (ff_down_buf_t without the flex data[] member)
// Note: the firmware payload and trailing BCC are appended manually.
#[derive(New)]
#[repr(C, packed)]
struct FuStructFocalMocDlHdr {
    head: u8,                  // MSG_MAGIC = 0x02
    ln: u16be,                 // big-endian length field
    cmd: FuFocalMocCmd,    // FU_FOCAL_MOC_CMD_FW_DOWNLOAD (0x33)
    magic: FuFocalMocMagic,
    seq: u8,                   // incrementing sequence number
}

// GetFwVersion response (CMD 0x30): header followed by NUL-terminated version string
#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructFocalMocVersionRsp {
    head: u8,
    ln: u16be,
    cmd: FuFocalMocCmd,
    version: [char; 60],
}
