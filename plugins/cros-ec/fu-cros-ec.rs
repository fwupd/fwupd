// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u16be)]
enum FuStructCrosEcFirstResponsePduHeaderType {
    Cr50, // must be 0 for backwards compatibility
    Common,
}

enum FuCrosEcUpdateExtraCmd {
    ImmediateReset,
    JumpToRw,
    StayInRo,
    UnlockRw,
    UnlockRollback,
    InjectEntropy,
    PairChallenge,
    TouchpadInfo,
    TouchpadDebug,
    ConsoleReadInit,
    ConsoleReadNext,
}

// Each RO or RW section of the new image can be in one of the following states
enum FuCrosEcFirmwareUpgradeStatus {
    NotNeeded,      // below or equal that on the target
    NotPossible,    // RO is newer, but can't be transferred due to target RW shortcomings
    Needed,         // section needs to be transferred to the target
}

// This is the frame format the host uses when sending update PDUs over USB.
// The PDUs are up to 1K bytes in size, they are fragmented into USB chunks of 64 bytes each and
// reassembled on the receive side before being passed to update function.
// The flash update function receives the unframed PDU body (starting at the cmd field below),
// and puts its reply into the same buffer the PDU was in.
#[derive(New)]
#[repr(C, packed)]
struct FuStructCrosEcUpdateFrameHeader {
    block_size: u32be,          // total frame size, including this field
    _cmd_block_digest: u32be,    // four bytes of the structure sha1 digest (or 0 where ignored)
    cmd_block_base: u32be,      // offset of this PDU into the flash SPI
    // payload goes here
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructCrosEcUpdateDone {
    value: u32be == 0xB007AB1E,
}

// various revision fields of the header created by the signer (cr50-specific).
// these fields are compared when deciding if versions of two images are the same or when deciding
// which one of the available images to run.
#[repr(C, packed)]
struct FuStructCrosEcSignedHeaderVersion {
    minor: u32be,
    major: u32be,
    epoch: u32be,
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructCrosEcFirstResponsePdu {
    return_value: u32be,
    header_type: FuStructCrosEcFirstResponsePduHeaderType,
    protocol_version: u16be,
    maximum_pdu_size: u32be,
    flash_protection: u32be,    // status
    offset: u32be,              // offset of the other region
    version: [char; 32],        // version string of the other region
    _min_rollback: u32be,       // minimum rollback version that RO will accept
    _key_version: u32be,        // RO public key version
}

#[derive(New, Getters)]
#[repr(C, packed)]
struct FuStructCrosEcTouchpadGetInfoResponsePdu {
    status: u8le,                   // Indicate if we get info from touchpad
    reserved: u8le,                 //Reserved for padding
    vendor: u16le,                  // Vendor USB id
    fw_address: u32le,              // Virtual address to touchpad firmware
    fw_size: u32le,                 // Size of the touchpad firmware
    allowed_fw_hash: [u8le; 32],    // Checksum of the entire touchpad firmware accepted by the EC image
    id: u16le,
    fw_version: u16le,
    fw_checksum: u16le,
}
