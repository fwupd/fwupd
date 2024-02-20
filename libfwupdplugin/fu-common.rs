// Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

enum FuCpuVendor {
    Unknown,
    Intel,
    Amd,
    Last,
}

#[derive(ToString)]
enum FuPowerState {
    Unknown,
    Ac,                     // On AC power
    AcCharging,             // Charging on AC
    AcFullyCharged,         // Fully charged on AC
    Battery,                // On system battery
    BatteryDischarging,     // System battery discharging
    BatteryEmpty,           // System battery empty
    Last,
}

#[derive(ToString)]
enum FuLidState {
    Unknown,
    Open,
    Closed,
    Last,
}

#[derive(ToString)]
enum FuDisplayState {
    Unknown,
    Connected,
    Disconnected,
    Last,
}
