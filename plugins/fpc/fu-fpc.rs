// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

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

#[derive(ValidateBytes, ParseBytes)]
struct FuStructFpcFf2Hdr {
    compat_sig: [char; 7] == "FPC0001",
    reserved: [u8; 20],
    blocks_num: u32le,
    reserved: [u8; 6],
}

#[repr(u8)]
enum FuFpcFf2BlockDir {
    Out = 0x0,
    In = 0x1,
}

// dfu_meta_content_hdr_t
#[derive(ParseBytes)]
struct FuStructFpcFf2BlockHdr {
    meta_type: u8 == 0xCD,
    meta_id: u8,
    dir: FuFpcFf2BlockDir,
}

// dfu_sec_link_t
#[derive(ParseBytes)]
struct FuStructFpcFf2BlockSec {
    header: u8 == 0xEE,
    type: u8,
    payload_len: u16le,
}
