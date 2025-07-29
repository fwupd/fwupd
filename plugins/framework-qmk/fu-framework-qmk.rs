// Copyright 2025 Framework Computer Inc
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuFrameworkQmkResetRequest {
    BootloaderJump      = 0x0B,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructFrameworkQmkResetRequest {
    report_id: u8 == 0x00,
    cmd: u8 == 0x0B,
    reserved: [u8; 30] = [0xFE; 30],
}

