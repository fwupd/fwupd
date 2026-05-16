// Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Enumeration of FwType Value of FW BIN Header
#[repr(u8)]
enum FuElanTsFwType {
    Unknown = 0x00,
    Ekt     = 0x01,
    Ektl    = 0x02,
}

// Enumeration of Debug Setting Value of FW BIN Header
#[repr(u32le)]
enum FuElanTsDebugSetting {
    none                  = 0x0000,
    enable_debug_msg      = 0x0001,
    skip_info_data_update = 0x0002,
    skip_remark_id_check  = 0x0004,
    force_update          = 0x0008,
}

// Structure of Elan TS FW BIN Header (LVFS Type 1)
#[derive(ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructElanTsFwBinHeaderLvfsType1 {
	// FwBinHeaderGUID: GUID of FW Bin Header
	// LVFS Type 1: {998DE210-1981-4EBF-874F-27BD6C264484}
	fw_bin_header_guid: Guid == "998de210-1981-4ebf-874f-27bd6c264484",
    fw_type: FuElanTsFwType,
    debug: u32le,
	// SHA-256 Hash of the Firmware Binary Data
    security_code: [u8; 32],
	// Size of Firmware Binary Data
    bin_size: u32le,
    reserved: [u8; 1024],
}

// Enumeration of IC Type of Elan Touchscreen Controller
#[repr(u8)]
enum FuElanTsIcType {
    unknown         = 0x00,
    elan_touch      = 0x01,
    elan_gen8_touch = 0x02,
}

// Enumeration of State of Elan Touchscreen Controller
#[repr(u8)]
enum FuElanTsState {
    unknown       = 0x00,
    normal_mode   = 0x01,
    Recovery_mode = 0x02,
}
