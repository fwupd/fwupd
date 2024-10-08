// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

struct FuStructIntelCvsFw {
    major: u32le,
    minor: u32le,
    hotfix: u32le,
    build: u32le,
}

struct FuStructIntelCvsId {
    vid: u16le,
    pid: u16le,
}

#[derive(ValidateStream, ParseStream)]
struct FuStructIntelCvsFirmwareHdr {
    magic_number: [char; 8] == "VISSOCFW",
    fw_version: FuStructIntelCvsFw,
    vid_pid: FuStructIntelCvsId,
    fw_offset: u32le,
    reserved: [u8; 220],
    header_checksum: u32le,
}

#[derive(ToString)]
#[repr(u8)]
enum FuIntelCvsDevstate {
    Unknown,
    Failed, // FIXME WHAT ARE THESE ENUMS
}

#[derive(ParseBytes)]
struct FuStructIntelCvsProbe {
    major: u32le,
    minor: u32le,
    hotfix: u32le,
    build: u32le,
    vid: u16le,
    pid: u16le,
    opid: u32le, // FIXME: what's this? type?
    dev_capabilities: u32le, // FIXME: type?
}

#[derive(New)]
struct FuStructIntelCvsWrite {
    max_download_time: u32le,
    max_flash_time: u32le,
    max_fwupd_retry_count: u32le,
    fw_bin_fd: i32,
}

#[derive(ParseBytes)]
struct FuStructIntelCvsStatus {
    dev_state: FuIntelCvsDevstate,
    fw_upd_retries: u32le,
    total_packets: u32le,
    num_packets_sent: u32le,
    fw_dl_finished: u8, // 0=in-progress, 1=finished
    fw_dl_status_code: u32le,
}
