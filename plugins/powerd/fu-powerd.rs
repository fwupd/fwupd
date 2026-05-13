// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuPowerdBatteryState {
    Unknown,
    Charging,
    Discharging,
    Empty,
    FullyCharged,
}

enum FuPowerdExternal {
    PowerUnknown,
    PowerAc,
    PowerUsb,
    PowerDisconnected,
}
