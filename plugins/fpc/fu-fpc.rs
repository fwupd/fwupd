// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuFpcDfuState {
    Dnbusy = 0x04,
}

#[derive(New, Getters)]
struct FuStructFpcDfu {
    status: u8,
    max_payload_size: u8,
    _reserved: [u8; 2],
    state: FuFpcDfuState,
    _reserved2: u8,
}
