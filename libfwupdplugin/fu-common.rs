// Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

enum CpuVendor {
    Unknown,
    Intel,
    Amd,
    Last,
}

#[derive(ToString)]
enum PowerState {
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
enum LidState {
    Unknown,
    Open,
    Closed,
    Last,
}

#[derive(ToString)]
enum DisplayState {
    Unknown,
    Connected,
    Disconnected,
    Last,
}
