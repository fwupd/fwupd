// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuDellDockI2cSpeed {
    250K,
    400K,
    800K,
}

enum FuDellDockMstCmd {
    EnableRemoteControl = 0x1,
    DisableRemoteControl = 0x2,
    Checksum = 0x11,
    EraseFlash = 0x14,
    Crc16Checksum = 0x17, // Cayenne specific
    ActivateFw = 0x18, // Cayenne specific
    WriteFlash = 0x20,
    WriteMemory = 0x21,
    ReadFlash = 0x30,
    ReadMemory = 0x31,
}

enum FuDellDockMstType {
    Unknown,
    Panamera,
    Cayenne,
}

enum FuDellDockMstBank {
    0,
    1,
    Esm,
    Cayenne,
}

#[repr(u16le)]
enum FuDellDockModuleType {
	W45Tbt = 1,
	W45Generic,
	W130Tbt,
	W130Dp,
	W130Universal,
	W240Trin,
	W210Dual,
	W130Usb4,
}

#[derive(ParseBytes)]
#[repr(C, packed)]
struct FuStructDellDockData {
	dock_configuration: u8,
	dock_type: u8,
	power_supply_wattage: u16le,
	module_type: FuDellDockModuleType,
	board_id: u16le,
	port0_dock_status: u16le,
	port1_dock_status: u16le,
	dock_firmware_pkg_ver: u32le,
	module_serial: u64le,
	original_module_serial: u64le,
	service_tag: [char; 7],
	marketing_name: [char; 64],
}
