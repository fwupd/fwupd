// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, Default)]
struct FuStructEp963xFirmwareHdr {
    reserved: [u8; 16],
    magic: [char; 5] == "EP963",
}

// byte = 0x07
#[derive(ToString)]
enum FuEp963xSmbusError {
    None = 0x00,
    Address = 0x01,
    NoAck = 0x02,
    Arbitration = 0x04,
    Command = 0x08,
    Timeout = 0x10,
    Busy = 0x20,
}
