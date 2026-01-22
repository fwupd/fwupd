// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u32)]
#[derive(ToString)]
enum FuHidBusType {
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

#[derive(ToString, FromString)]
enum FuHidItemTag {
    Unknown                 = 0b0,
    // Main
    Input                   = 0b1000_00,
    Output                  = 0b1001_00,
    Feature                 = 0b1011_00,
    Collection              = 0b1010_00,
    EndCollection           = 0b1100_00,
    // Global
    UsagePage               = 0b0000_01,
    LogicalMinimum          = 0b0001_01,
    LogicalMaximum          = 0b0010_01,
    PhysicalMinimum         = 0b0011_01,
    PhysicalMaximum         = 0b0100_01,
    Unit                    = 0b0101_01,
    ReportSize              = 0b0111_01,
    ReportId                = 0b1000_01,
    ReportCount             = 0b1001_01,
    Push                    = 0b1010_01,
    Pop                     = 0b1011_01,
    // Local
    Usage                   = 0b0000_10,
    UsageMinimum            = 0b0001_10,
    UsageMaximum            = 0b0010_10,
    DesignatorIndex         = 0b0011_10,
    DesignatorMinimum       = 0b0100_10,
    DesignatorMaximum       = 0b0101_10,
    StringIndex             = 0b0111_10,
    StringMinimum           = 0b1000_10,
    StringMaximum           = 0b1001_10,
    // 'just' supported
    Long                    = 0b1111,
}

#[derive(ToString)]
enum FuHidItemKind {
    Main,
    Global,
    Local,
}

// flags used when calling GetReport() and SetReport()
enum FuHidDeviceFlags {
    None = 0,
    AllowTrunc = 1 << 0,
    IsFeature = 1 << 1,
    RetryFailure = 1 << 2,
    NoKernelUnbind = 1 << 3,
    NoKernelRebind = 1 << 4,
    UseInterruptTransfer = 1 << 5,
    AutodetectEps = 1 << 6,
}
