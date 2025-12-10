// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The CPU vendor.
// Since: 1.5.5
enum FuCpuVendor {
    Unknown,
    Intel,
    Amd,
}

// The system power state.
//
// This does not have to be exactly what the battery is doing, but is supposed to represent the
// 40,000ft view of the system power state.
//
// For example, it is perfectly correct to set %FU_POWER_STATE_AC if the system is connected to
// AC power, but the battery cells are discharging for health or for other performance reasons.
// Since: 1.8.11
#[derive(ToString)]
enum FuPowerState {
    Unknown,
    Ac,                     // On AC power
    Battery,                // On system battery
}

// The device lid state.
// Since: 1.7.4
#[derive(ToString)]
enum FuLidState {
    Unknown,
    Open,
    Closed,
}

// The device lid state.
// Since: 1.9.6
#[derive(ToString, FromString)]
enum FuDisplayState {
    Unknown,
    Connected,
    Disconnected,
}
