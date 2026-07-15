// Copyright 2024 FocalTech Systems Co., Ltd.
// SPDX-License-Identifier: LGPL-2.1-or-later

// Command codes
#[repr(u8)]
enum FuFocalMocCmd {
    GetFwVersion = 0x30,  // request firmware version string
    SetBootMode  = 0x32,  // switch device mode; data[0] = FuFocalMocBootMode
    FwDownload   = 0x33,  // firmware download (3-phase: SHO → STX×N → EOT)
    Ack          = 0x04,  // device acknowledgment (response to all commands)
}

// Boot mode argument for SetBootMode
#[repr(u8)]
enum FuFocalMocBootMode {
    EnterApp     = 0x00,  // return to normal application mode
    EnterBoot    = 0x01,  // enter bootloader (IAP) mode for firmware update
    EnterRomBoot = 0x02,  // enter ROM bootloader mode
}

// Magic byte in the firmware-download packet
#[repr(u8)]
enum FuFocalMocMagic {
    Sho = 0x01,  // Start-of-Header: first packet, carries filename + size + CRC32
    Stx = 0x02,  // Start-of-Text: middle data block (1024 bytes each)
    Eot = 0x04,  // End-of-Text: last (possibly partial) data block
}

// Standard command request header
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalMocCmdReq {
    head: u8 == 0x02,
    ln: u16be,             // payload length (data bytes + 1 for BCC)
    cmd: FuFocalMocCmd,
}

// Device response header
#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalMocCmdRsp {
    head: u8 == 0x02,
    ln: u16be,
    st: FuFocalMocCmd == Ack,
}

// Firmware-download packet header
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFocalMocDlHdr {
    head: u8 == 0x02,
    ln: u16be,
    cmd: FuFocalMocCmd = FwDownload,
    magic: FuFocalMocMagic,
    seq: u8,                   // incrementing sequence number
}

// GetFwVersion response
#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructFocalMocVersionRsp {
    head: u8 == 0x02,
    ln: u16be,
    st: FuFocalMocCmd == Ack,
    version: [char; 59],
}

// SHO packet data payload
#[derive(New, Setters)]
#[repr(C, packed)]
struct FuStructFocalMocShoData {
    filename: [char; 64],
    filesize: u32be,
    crc32: u32be,
    _padding: [u8; 56],
}
