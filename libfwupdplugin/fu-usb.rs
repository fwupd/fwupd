// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuUsbDirection {
    DeviceToHost, // IN
    HostToDevice, // OUT
}

enum FuUsbRequestType {
    Standard,
    Class,
    Vendor,
    Reserved,
}

enum FuUsbRecipient {
    Device,
    Interface,
    Endpoint,
    Other,
}

#[derive(ToString)]
enum FuUsbClass {
    InterfaceDesc = 0x00,
    Audio = 0x01,
    Communications = 0x02,
    Hid = 0x03,
    Physical = 0x05,
    Image = 0x06,
    Printer = 0x07,
    MassStorage = 0x08,
    Hub = 0x09,
    Cdc_data = 0x0A,
    SmartCard = 0x0B,
    ContentSecurity = 0x0D,
    Video = 0x0E,
    PersonalHealthcare = 0x0F,
    AudioVideo = 0x10,
    Billboard = 0x11,
    Diagnostic = 0xDC,
    WirelessController = 0xE0,
    Miscellaneous = 0xEF,
    ApplicationSpecific = 0xFE,
    VendorSpecific = 0xFF,
}

enum FuUsbLangid {
    Invalid = 0x0000,
    EnglishUnitedStates = 0x0409,
}
