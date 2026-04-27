// Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
// SPDX-License-Identifier: LGPL-2.1-or-later

// =========================================================================
// SECTION 0: ELAN TS CHIP CONFIGURATION
// =========================================================================

// Enumeration of FwType Value of FW BIN Header
#[repr(u8)]
enum FuElanTsFwType {
    unknown = 0x00,
    ekt = 0x01,
    ektl = 0x02,
}

// Enumeration of Debug Setting Value of FW BIN Header
#[repr(u32le)]
enum FuElanTsDebugSetting {
    none = 0,
    enable_debug_msg = 1 << 0,
    skip_info_data_update = 1 << 1,
    skip_remark_id_check = 1 << 2,
    force_update = 1 << 3,
}

// Enumeration of State of Elan Touchscreen Controller
#[repr(u8)]
enum FuElanTsState {
    unknown = 0x00,
    normal_mode = 0x01,
    recovery_mode = 0x02,
}

// Solution ID (High Byte of FW Version) with multi-IC and repack suffix mapping
#[repr(u8)]
enum FuElanTsSolutionId {
    ekth6315x1 = 0x61,         // Single eKTH6315 IC solution
    ekth6315x2 = 0x62,         // eKTH6315 Multi-IC solution (2 cascading ICs)
    ekth6315_to_5015m = 0x59,  // eKTH6315 remarked to 5015M Repack solution
    ekth6315_to_3915p = 0x15,  // eKTH6315 remarked to 3915P Repack solution
    ekth6308x1 = 0x63,         // Single eKTH6308 IC solution
    ekth7315x1 = 0x64,         // Single eKTH7315 IC solution
    ekth7315x2 = 0x65,         // eKTH7315 Multi-IC solution (2 cascading ICs)
    ekth7318x1 = 0x67,         // Single eKTH7318 IC solution
}

// High Byte of Boot Code Version with multi-IC and repack suffix mapping
#[repr(u8)]
enum FuElanTsBcVerHighByte {
    ekta6315x1 = 0xA7,         // Single eKTA6315 IC solution
    ekth6315_to_5015m = 0xE6,  // eKTH6315 remarked to 5015M Repack solution
    ekth6315_to_3915p = 0xF6,  // eKTH6315 remarked to 3915P Repack solution
    ekta6308x1 = 0xA8,         // Single eKTA6308 IC solution
    ekta7315x1 = 0xA9,         // Single eKTA7315 IC solution
}

// Structure of Elan TS FW BIN Header (LVFS Type 1)
#[derive(ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructElanTsFwBinHeaderLvfsType1 {
    fw_bin_header_guid: Guid == "998de210-1981-4ebf-874f-27bd6c264484",
    fw_type: FuElanTsFwType,
    debug: u32le,
    security_code: [u8; 32],
    bin_size: u32le,
    reserved: [u8; 1024],
}

// =========================================================================
// SECTION 1: ELAN TS HID PROTOCOL OVERHEAD & INPUT REPORT
// =========================================================================

// raw hid input report wrapper structure with 2-byte protocol header
#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsInputReport {
    report_id: u8,
    data_len: u8,
    payload: [u8; 63],
}

// =========================================================================
// SECTION 2: ELAN TS VENDOR COMMAND (1-BYTE) & RESPONSE
// =========================================================================

// Enumeration of Single-byte Vendor Commands
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsVendorCmd {
    ReadHelloPktAndBcVer = 0x18,
    FlashWrite = 0x22,
}

// Hello Packet values for Elan Touchscreen
#[repr(u8)]
enum FuElanTsHelloPacket {
    normal_mode = 0x20,
    recovery_mode = 0x56,
}

// Single-byte Vendor Command Wrapper (1 Byte)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsVendorCmd {
    cmd: FuElanTsVendorCmd,
}

// Response structure for Hello and Boot Code Version command (4 Bytes)
#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsHelloPktAndBcVerRsp {
    hello_packet: FuElanTsHelloPacket,
    reserved: u8,
    bc_version: u16be,
}

// Response structure for Flash Write command (2 Bytes)
#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsFlashWriteRsp {
    payload: u16be == 0xAAAA,
}

// I2C Address Handshake Command Structure (1 Byte)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsI2cAddrCmd {
    // Automatically initialized to ELAN_TS_I2C_ADDR_7BIT (0x10)
    // Note: Replace with the actual constant name or literal in your .rs
    addr_7bit: u8 == 0x10, 
}

// I2C Address Handshake Response Structure (1 Byte)
#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructElanTsI2cAddrRsp {
    // Automatically validated against ELAN_TS_I2C_ADDR (0x20)
    // Note: Replace with the actual constant name or literal in your .rs
    addr_8bit: u8 == 0x20,
}

// =========================================================================
// SECTION 3: ELAN TS COMMAND (4-BYTE) & RESPONSE
// =========================================================================

// Command types
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsCmdType {
    read_cmd = 0x53,
    write_cmd = 0x54,
    show_bulk_rom_data_cmd = 0x59,
    read_rom_data_cmd = 0x96,
}

// Command identifiers for write operations
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsWriteCmdId {
    enter_iap = 0x00,
    rek = 0x29,
    write_flash_key = 0xC0,
}

// Response types
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsRspType {
    read_cmd_rsp = 0x52,
    read_rom_cmd_rsp = 0x95,
    show_bulk_rom_data_cmd_rsp = 0x99,
}

// Command identifiers for read operations
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsReadCmdId {
    fw_version = 0x00,
    boot_code_version = 0x10,
    test_solution_version = 0xE0,
    fw_id = 0xF0,
}

// Common 4-Byte Write Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsWriteCmd {
    cmd_type: FuElanTsCmdType == write_cmd,
    cmd_id: FuElanTsWriteCmdId,
    payload: u16be == 0x0001,
}

// Common 4-Byte Read Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsReadCmd {
    cmd_type: FuElanTsCmdType == read_cmd,
    cmd_id: FuElanTsReadCmdId,
    payload: u16be == 0x0001,
}

// Enter IAP mode Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsEnterIapCmd {
    cmd_type: FuElanTsCmdType == write_cmd,
    cmd_id: FuElanTsWriteCmdId == enter_iap,
    payload: u16be == 0x1234,
}

// Write Flash Key Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsWriteFlashKeyCmd {
    cmd_type: FuElanTsCmdType == write_cmd,
    cmd_id: FuElanTsWriteCmdId == write_flash_key,
    payload: u16be == 0xE15A,
}

// Re-Calibration (REK) Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsRekCmd {
    cmd_type: FuElanTsCmdType == write_cmd,
    cmd_id: FuElanTsWriteCmdId == rek,
    payload: u16be == 0x0001,
}

// Response structure for Calibration command (4 Bytes)
#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructElanTsCalibrationRsp {
    payload: u32be == 0x66666666,
}

// Boot Code Version Command Structure (4 Bytes)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsBcVersionCmd {
    cmd_type: FuElanTsCmdType == read_cmd,
    cmd_id: FuElanTsReadCmdId == boot_code_version,
    payload: u16be == 0x0001,
}

// Boot Code Version Response Structure (4 Bytes)
#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructElanTsBcVersionRsp {
    rsp_type: FuElanTsRspType == read_cmd_rsp,
    payload: [u8; 3],
}

// Firmware ID Command Structure (4 Bytes)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsFwIdCmd {
    cmd_type: FuElanTsCmdType == read_cmd,
    cmd_id: FuElanTsReadCmdId == fw_id,
    payload: u16be == 0x0001,
}

// Firmware ID Response Structure (4 Bytes)
#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsFwIdRsp {
    rsp_type: FuElanTsRspType == read_cmd_rsp,
    payload: [u8; 3],
}

// Firmware Version Command Structure (4 Bytes)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsFwVersionCmd {
    cmd_type: FuElanTsCmdType == read_cmd,
    cmd_id: FuElanTsReadCmdId == fw_version,
    payload: u16be == 0x0001,
}

// Firmware Version Response Structure (4 Bytes)
#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsFwVersionRsp {
    rsp_type: FuElanTsRspType == read_cmd_rsp,
    payload: [u8; 3],
}

// Test-Solution Version Command Structure (4 Bytes)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsTestSolutionVersionCmd {
    cmd_type: FuElanTsCmdType == read_cmd,
    cmd_id: FuElanTsReadCmdId == test_solution_version,
    payload: u16be == 0x0001,
}

// Test-Solution Version Response Structure (4 Bytes)
#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsTestSolutionVersionRsp {
    rsp_type: FuElanTsRspType == read_cmd_rsp,
    payload: [u8; 3],
}

// Enter Test Mode Command Structure (4 Bytes)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsEnterTestModeCmd {
    payload: u32be == 0x55555555,
}

// Exit Test Mode Command Structure (4 Bytes)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsExitTestModeCmd {
    payload: u32be == 0xA5A5A5A5,
}

// =========================================================================
// SECTION 4: ELAN TS COMMAND (6-BYTE) & RESPONSE
// =========================================================================

// mode specification based on ic generation for read rom data command
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsReadRomCmdMode {
    ekth53xx = 0x11,
    ekth63xx_73xx = 0x21,
}

// operating modes used in Show Bulk ROM Data Command
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsShowBulkRomMode {
    boot_code = 0x00,
    main_code = 0x10,
}

// read rom data command structure (6 bytes)
#[derive(New, Default, Setters)]
#[repr(C, packed)]
struct FuStructElanTsReadRomCmd {
    cmd_type: FuElanTsCmdType == read_rom_data_cmd,
    mem_addr: u16be,
    pad: u16be == 0x0000,
    mode: FuElanTsReadRomCmdMode = ekth53xx,
}

// read rom data response structure (6 bytes)
#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsReadRomRsp {
    header: FuElanTsRspType == read_rom_cmd_rsp,
    mem_addr: u16be,
    rom_data: u16be,
    mode: FuElanTsReadRomCmdMode,
}

// show bulk ROM data command (6 Bytes)
#[derive(New, Default, Setters)]
#[repr(C, packed)]
struct FuStructElanTsShowBulkRomDataCmd {
    cmd_type: FuElanTsCmdType == show_bulk_rom_data_cmd,
    mode: FuElanTsShowBulkRomMode = main_code,
    mem_addr: u16be,
    data_size_words: u16be,
}

// show bulk rom data response frame structure (fixed 63 bytes report payload)
#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsShowBulkRomRsp {
    header: FuElanTsRspType == show_bulk_rom_data_cmd_rsp, // 0x99 (1 byte)
    frame_index: u8,                                       // 0x00, 0x01, ... (1 byte)
    data_size_words: u8,                                   // e.g. 0x1E = 30 words (1 byte)
    data: [u8; 60],                                        // fixed trailing buffer (60 bytes)
}
