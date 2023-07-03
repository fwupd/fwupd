// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum LogitechBulkcontrollerDeviceState {
    Unknown = -1,
    Offline,
    Online,
    Idle,
    InUse,
    AudioOnly,
    Enumerating,
}

#[derive(ToString)]
enum LogitechBulkcontrollerUpdateState {
    Unknown = -1,
    Current,
    Available,
    Starting = 3,
    Downloading,
    Ready,
    Updating,
    Scheduled,
    Error,
}
