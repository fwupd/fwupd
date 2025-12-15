// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString, FromString)]
enum FuCorsairDeviceKind {
    Unknown,
    Mouse,
    Receiver,
}

enum FuCorsairBpProperty {
    Mode = 0x03,
    BatteryLevel = 0x0F,
    Version = 0x13,
    BootloaderVersion = 0x14,
    Subdevices = 0x36,
}

enum FuCorsairDeviceMode {
    Application = 0x01,
    Bootloader = 0x03,
}
