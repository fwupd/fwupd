// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuCpuVendor {
    Unknown,
    Intel,
    Amd,
}

#[derive(ToString)]
enum FuPowerState {
    Unknown,
    Ac,                     // On AC power
    Battery,                // On system battery
}

#[derive(ToString)]
enum FuLidState {
    Unknown,
    Open,
    Closed,
}

#[derive(ToString, FromString)]
enum FuDisplayState {
    Unknown,
    Connected,
    Disconnected,
}
