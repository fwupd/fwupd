// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct Oprom {
    signature: u16le: const=0xAA55,
    image_size: u16le,		// of 512 bytes
    init_func_entry_point: u32le,
    subsystem: u16le,
    machine_type: u16le,
    compression_type: u16le,
    _reserved: 8u8,
    efi_image_offset: u16le,
    pci_header_offset: u16le: default=$struct_size,
    expansion_header_offset: u16le,
}
#[derive(New, Validate, Parse)]
struct OpromPci {
    signature: u32le: const=0x52494350,
    vendor_id: u16le,
    device_id: u16le,
    device_list_pointer: u16le,
    structure_length: u16le,
    structure_revision: u8,
    class_code: u24,
    image_length: u16le,		// of 512 bytes
    image_revision: u16le,
    code_type: u8,
    indicator: u8,
    max_runtime_image_length: u16le,
    conf_util_code_header_pointer: u16le,
    dmtf_clp_entry_point_pointer: u16le,
}
