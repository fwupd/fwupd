// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u16le)]
enum FuOpromMachineType {
    X64,
}

#[repr(u16le)]
enum FuOpromSubsystem {
    EfiBootSrvDrv,
}

#[repr(u16le)]
enum FuOpromCompressionType {
    None,
}

#[derive(New, ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructOprom {
    signature: u16le == 0xAA55,
    image_size: u16le,		// of 512 bytes
    init_func_entry_point: u32le,
    subsystem: FuOpromSubsystem,
    machine_type: FuOpromMachineType,
    compression_type: FuOpromCompressionType,
    _reserved: [u8; 8],
    efi_image_offset: u16le,
    pci_header_offset: u16le = $struct_size,
    expansion_header_offset: u16le,
}

#[derive(New, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructOpromPci {
    signature: u32le == 0x52494350,
    vendor_id: u16le,
    device_id: u16le,
    device_list_pointer: u16le,
    structure_length: u16le,
    structure_revision: u8,
    class_code: u24le,
    image_length: u16le,		// of 512 bytes
    image_revision: u16le,
    code_type: u8,
    indicator: u8,
    max_runtime_image_length: u16le,
    conf_util_code_header_pointer: u16le,
    dmtf_clp_entry_point_pointer: u16le,
}
