// Copyright 2026 Mario Limonciello <superm1@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// the values of /sys/class/firmware/<name>/status
#[derive(ToString, FromString)]
#[repr(u8)]
enum FuFwUploadStatus {
    Idle,
    Receiving,
    Preparing,
    Transferring,
    Programming,
}

// the second half of /sys/class/firmware/<name>/error, which is "<status>:<error>"
#[derive(ToString, FromString)]
#[repr(u8)]
enum FuFwUploadError {
    None,
    HwError,
    Timeout,
    UserAbort,
    DeviceBusy,
    InvalidFileSize,
    ReadWriteError,
    FlashWearout,
    FirmwareInvalid,
}
