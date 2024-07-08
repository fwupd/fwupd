// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream)]
struct FuStructParadeUsbhubHdr {
    signature: u16be == 0x55AA,
}

enum FuParadeUsbhubDeviceRequest {
    Read = 0x40,
    Write = 0x41,
}

enum FuParadeUsbhubDeviceAddr {
    Status      = 0x5000,
    Data        = 0x5001, // u32
    SpiAddr     = 0x5005, // u24
    SramAddr    = 0x5008, // u16
    DmaSize     = 0x500C,
    ReadSize    = 0x500D,
    DbiTimeout  = 0x5819,
    UfpDisconnect = 0x584B,
    SpiMasterAcquire = 0x5824,
    SpiMaster   = 0x5826,
    SramPage    = 0x5879,
    VersionA    = 0x5C0E,
    VersionB    = 0x5C0F,
    VersionC    = 0x5C11,
    VersionD    = 0x5C12,
}

enum FuParadeUsbhubDeviceStatusFlag {
    Write       = 0b00000001,
    TriggerSpi  = 0b00000010,
    TriggerDbi  = 0b00000100,
    Checksum    = 0b00001000,
    SpiDone     = 0b10000000,
}
