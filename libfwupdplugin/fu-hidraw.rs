// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u32)]
#[derive(ToString)]
enum FuHidrawBusType {
    Unknown      = 0x00,
    Pci          = 0x01,
    Isapnp       = 0x02,
    Usb          = 0x03,
    Hil          = 0x04,
    Bluetooth    = 0x05,
    Virtual      = 0x06,
    Isa          = 0x10,
    I8042        = 0x11,
    Xtkbd        = 0x12,
    Rs232        = 0x13,
    Gameport     = 0x14,
    Parport      = 0x15,
    Amiga        = 0x16,
    Adb          = 0x17,
    I2c          = 0x18,
    Host         = 0x19,
    Gsc          = 0x1A,
    Atari        = 0x1B,
    Spi          = 0x1C,
    Rmi          = 0x1D,
    Cec          = 0x1E,
    IntelIshtp   = 0x1F,
    AmdSfh       = 0x20,
}
