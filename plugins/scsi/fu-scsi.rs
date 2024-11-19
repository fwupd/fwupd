// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuScsiSenseKey {
    NoSense = 0x00,
    RecoveredError = 0x01,
    NotReady = 0x02,
    MediumError = 0x03,
    HardwareError = 0x04,
    IllegalRequest = 0x05,
    UnitAttention = 0x06,
    DataProtect = 0x07,
    BlankCheck = 0x08,
    VendorSpecific = 0x09,
    CopyAborted = 0x0A,
    AbortedCommand = 0x0B,
    Equal = 0x0C,
    VolumeOverflow = 0x0D,
    Miscompare = 0x0E,
}

// see https://tldp.org/HOWTO/archived/SCSI-Programming-HOWTO/SCSI-Programming-HOWTO-9.html
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructScsiInquiry {
    reserved: [u8; 8],
    vendor_id: [char; 8],
    product_id: [char; 16],
    product_rev: [char; 4],
}
