// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuEgisMocStatus {
    Success = 0x9000,
}

enum FuEgisMocOpMode {
    Bootloader = 0x0B,
}

#[repr(u8)]
enum FuEgisMocCla {
    ApduGeneral = 0x50,
}

#[repr(u8)]
enum FuEgisMocCmd {
    OpModeGet    = 0x52,
    ChallengeGet = 0x54,
    EnterOtaMode = 0x58,
    OtaWrite     = 0x5A,
    OtaFinal     = 0x5C,
    ApduVersion  = 0x7F,
}

#[derive(New, Default)]
struct FuStructEgisMocCmdReq {
    cla: FuEgisMocCla == ApduGeneral,
    ins: FuEgisMocCmd,
    _p1: u8,
    _p2: u8,
    _lc1: u8,
    _lc2: u8,
    lc3: u8,
}

struct FuStructEgisMocVersionInfo {
    platform: [u8; 4],
    _dot1: u8,
    major_version: u8,
    _dot2: u8,
    minor_version: u8,
    _dot3: u8,
    revision: [u8; 2],
}

#[derive(New, Parse)]
struct FuStructEgisMocPkgHeader {
    sync: u32be,
    id: u32be,
    chksum: u16be,
    len: u32be,
}
