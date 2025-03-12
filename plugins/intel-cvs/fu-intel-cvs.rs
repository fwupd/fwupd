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

#[derive(ValidateStream, ParseStream, Default)]
struct FuStructIntelCvsFirmwareHdr {
    magic_number: [char; 8] == "VISSOCFW",
    fw_version: FuStructIntelCvsFw,
    vid_pid: FuStructIntelCvsId,
    fw_offset: u32le,
    reserved: [u8; 220],
    header_checksum: u32le,
}

// CV SoC device status */
#[repr(u8)]
enum FuIntelCvsDeviceState {
    DeviceOff       = 0,
    PrivacyOn       = 1 << 0,
    DeviceOn        = 1 << 1,
    SensorOwner     = 1 << 2,
    DeviceDwnldState = 1 << 4,
    DeviceDwnldError = 1 << 6,
    DeviceDwnldBusy = 1 << 7,
};

// CVS sensor Status */
#[repr(u8)]
enum FuIntelCvsSensorState {
    Released        = 0,
    VisionAcquired  = 1 << 0,
    IpuAcquired     = 1 << 1,
};

// CVS driver Status */
#[repr(u8)]
enum FuIntelCvsState {
    Init            = 0,
    FwDownloading   = 1 << 1,
    FwFlashing      = 1 << 3,
    Stopping        = 1 << 7,
}

#[repr(u8)]
enum FuIntelCvsCameraOwner {
    None,
    Cvs,
    Ipu,
}

#[repr(u32le)]
enum FuStructIntelCvsDevCapability {
    CvPowerDomain           = 1 << 10,
    NocameraDuringFwupdate  = 1 << 11,
    FwupdateResetRequired   = 1 << 12,
    Privacy2visiondriver    = 1 << 13,
    FwAntirollback          = 1 << 14,
    HostMipiConfigRequired  = 1 << 15,
}

#[derive(ParseBytes)]
struct FuStructIntelCvsProbe {
    major: u32le,
    minor: u32le,
    hotfix: u32le,
    build: u32le,
    vid: u16le,
    pid: u16le,
    opid: u32le, // unused by most ODMs
    dev_capabilities: FuStructIntelCvsDevCapability,
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
    dev_state: FuIntelCvsDeviceState,
    fw_upd_retries: u32le,
    total_packets: u32le,
    num_packets_sent: u32le,
    fw_dl_finished: u8, // 0=in-progress, 1=finished
    fw_dl_status_code: u32le,
}
