// Copyright 2026 Harris Tai <harris_tai@pixart.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// 0x57: NdtUpdate bit fields (user bank 0), the STATUS code is in bits 7:2
#[repr(u8)]
enum FuPixartTpNdtUpdateBit {
    Ready = 1 << 0,
    Start = 1 << 1,
}

// NDT HID report IDs
#[repr(u8)]
enum FuPixartTpNdtReportId {
    Update = 0x60,
}

// NDT 0x60 report opcodes
#[repr(u8)]
enum FuPixartTpNdtOpcode {
    Update  = 1,
    Version = 2,
}

#[repr(u8)]
enum FuPixartTpNdtDirection {
    Write = 1,
}

#[repr(u8)]
enum FuPixartTpNdtIsLast {
    No  = 0x00,
    Yes = 0x01,
}

// NDT update session result (status bits of 0x57)
#[derive(ToString)]
#[repr(u8)]
enum FuPixartTpNdtStatus {
    Ok              = 0x00,
    BadOpcode       = 0x01,
    BadLen          = 0x02,
    HeaderParseFail = 0x03,
    ChipIdFail      = 0x04,
    SramSetupFail   = 0x05,
    EraseFail       = 0x06,
    WriteFail       = 0x07,
    JumpFail        = 0x08,
    VersionMismatch = 0x09,
    HandshakeErr    = 0x0a,
}

// NDT update report (0x60, opcode 01, set_feature), always 257 bytes incl. the report ID
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpNdtUpdateReport {
    report_id: FuPixartTpNdtReportId == Update,
    opcode:    FuPixartTpNdtOpcode == Update,
    direction: FuPixartTpNdtDirection == Write,
    len:       u8,
    is_last:   FuPixartTpNdtIsLast,
    data:      [u8; 252],
}

// NDT version request (0x60, opcode 02, set_feature), padded to the 257 byte report
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpNdtVersionReq {
    report_id: FuPixartTpNdtReportId == Update,
    opcode:    FuPixartTpNdtOpcode == Version,
    reserved:  [u8; 255],
}

// NDT version reply (0x60, get_feature), version is b0.b1.b2.b3 with b0 as the MSB
#[derive(New, Validate, Getters, Default)]
#[repr(C, packed)]
struct FuStructPixartTpNdtVersionReply {
    report_id: FuPixartTpNdtReportId == Update,
    version:   u32be,
    reserved:  [u8; 252],
}

// NDT image header (72 bytes), followed by valid_data_len bytes of payload
#[derive(New, ParseBytes, Getters, Setters, Default)]
#[repr(C, packed)]
struct FuStructPixartTpNdtImageHeader {
    pre_header:     [u8; 8],
    para_data_type: u32le == 0x21,
    reserved:       [u8; 48],
    sw_version:     u32le,
    download_addr:  u32le == 0x01001000,
    valid_data_len: u32le,
}
