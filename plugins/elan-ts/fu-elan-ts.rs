// Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
// SPDX-License-Identifier: LGPL-2.1-or-later

// =========================================================================
// SECTION 0: ELAN TS CHIP CONFIGURATION
// =========================================================================

enum FuElanTsPid {
    Bridge      = 0x0007,
    BridgeB     = 0x000B,
}

#[repr(u8)]
enum FuElanTsReportId {
    Finger      = 0x01,
    Input       = 0x02,
    Output      = 0x03,
    Pen         = 0x07,
    PenDebug    = 0x17,
}

enum FuElanTsMemAddr {
    InfoPageWrite       = 0x0040,
    InfoRom             = 0x8000,
    RemarkId            = 0x801F,
    Page1               = 0x8040,
    UpdateCounter       = 0x8060,
    LastUpdateYear      = 0x8061,
    LastUpdateMonthDay  = 0x8062,
    LastUpdateTime      = 0x8063,
    InfoRomFwid         = 0x8080,
}

// info page data, starting at 0x8040
#[derive(Parse, Setters)]
struct FuStructElanTsInfoPage {
    _reserved1: [u8; 0x40],
    update_counter: u16le,
    update_year: u16le, // BCD
    update_month: u8,   // BCD
    update_day: u8,     // BCD
    update_hour: u8,    // BCD
    update_minute: u8,  // BCD
    _reserved2: [u8; 56],
}

// Enumeration of FwType Value of FW BIN Header
#[repr(u8)]
enum FuElanTsFwType {
    Unknown = 0x00,
    Ekt = 0x01,
    Ektl = 0x02,
}

// Enumeration of Debug Setting Value of FW BIN Header
#[repr(u32le)]
enum FuElanTsDebugSetting {
    None = 0,
    EnableDebugMsg = 1 << 0,
    SkipInfoDataUpdate = 1 << 1,
    SkipRemarkIdCheck = 1 << 2,
    ForceUpdate = 1 << 3,
}

// Enumeration of State of Elan Touchscreen Controller
#[repr(u8)]
enum FuElanTsState {
    Unknown = 0x00,
    NormalMode = 0x01,
    RecoveryMode = 0x02,
}

// Solution ID (High Byte of FW Version) with multi-IC and repack suffix mapping
#[repr(u8)]
enum FuElanTsSolutionId {
    Ekth6315x1 = 0x61,         // Single eKTH6315 IC solution
    Ekth6315x2 = 0x62,         // eKTH6315 Multi-IC solution (2 cascading ICs)
    Ekth6315To_5015m = 0x59,   // eKTH6315 remarked to 5015M Repack solution
    Ekth6315To_3915p = 0x15,   // eKTH6315 remarked to 3915P Repack solution
    Ekth6308x1 = 0x63,         // Single eKTH6308 IC solution
    Ekth7315x1 = 0x64,         // Single eKTH7315 IC solution
    Ekth7315x2 = 0x65,         // eKTH7315 Multi-IC solution (2 cascading ICs)
    Ekth7318x1 = 0x67,         // Single eKTH7318 IC solution
}

// High Byte of Boot Code Version with multi-IC and repack suffix mapping
#[repr(u8)]
enum FuElanTsBcVerHighByte {
    Ekta6315x1 = 0xA7,         // Single eKTA6315 IC solution
    Ekth6315To_5015m = 0xE6,   // eKTH6315 remarked to 5015M Repack solution
    Ekth6315To_3915p = 0xF6,   // eKTH6315 remarked to 3915P Repack solution
    Ekta6308x1 = 0xA8,         // Single eKTA6308 IC solution
    Ekta7315x1 = 0xA9,         // Single eKTA7315 IC solution
    Ekth7318x1 = 0xAA,         // Single eKTH7318 IC solution
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

#[derive(Parse, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsInputReport {
    report_id: FuElanTsReportId,
    data_len: u8,
    payload: [u8; 63],
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsHidWriteFrameData {
    report_id: FuElanTsReportId == Output,
    subcommand: u8 == 0x21,
    offset: u16be,
    data_len: u8,
    // payload here
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsHidWriteCommand {
    report_id: FuElanTsReportId = Output,
    subcommand: u8 == 0x00, // bridge command
    data_len: u8,
    // payload here
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
    NormalMode = 0x20,
    RecoveryMode = 0x56,
}

// Single-byte Vendor Command Wrapper (1 Byte)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsVendorCmd {
    report_id: FuElanTsReportId == Output,
    cmd: FuElanTsVendorCmd,
}

// Response structure for Hello and Boot Code Version command
#[derive(ParseBytes, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsHelloPktAndBcVerRsp {
    hello_packet: FuElanTsHelloPacket,
    reserved: u8,
    bc_version: u16be,
}

// Response structure for Flash Write command (2 Bytes)
#[derive(ParseBytes, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsFlashWriteRsp {
    payload: u16be == 0xAAAA,
}

// I2C Address Handshake Command Structure (1 Byte)
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsI2cAddrCmd {
    // Automatically initialized to ELAN_TS_I2C_ADDR_7BIT
    // Note: Replace with the actual constant name or literal in your .rs
    addr_7bit: u8 == 0x10,
}

// I2C Address Handshake Response Structure (1 Byte)
#[derive(ParseBytes, Default)]
#[repr(C, packed)]
struct FuStructElanTsI2cAddrRsp {
    // Automatically validated against ELAN_TS_I2C_ADDR
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
    ReadCmd = 0x53,
    WriteCmd = 0x54,
    ShowBulkRomDataCmd = 0x59,
    ReadRomDataCmd = 0x96,
}

// Command identifiers for write operations
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsWriteCmdId {
    EnterIap = 0x00,
    Rek = 0x29,
    WriteFlashKey = 0xC0,
}

// Response types
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsRspType {
    ReadCmdRsp = 0x52,
    ReadRomCmdRsp = 0x95,
    ShowBulkRomDataCmdRsp = 0x99,
}

// Command identifiers for read operations
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsReadCmdId {
    FwVersion = 0x00,
    BootCodeVersion = 0x10,
    TestSolutionVersion = 0xE0,
    FwId = 0xF0,
}

// Common 4-Byte Write Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsWriteCmd {
    cmd_type: FuElanTsCmdType == WriteCmd,
    cmd_id: FuElanTsWriteCmdId,
    payload: u16be == 0x0001,
}

// Common 4-Byte Read Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsReadCmd {
    cmd_type: FuElanTsCmdType == ReadCmd,
    cmd_id: FuElanTsReadCmdId,
    payload: u16be == 0x0001,
}

// Enter IAP mode Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsEnterIapCmd {
    cmd_type: FuElanTsCmdType == WriteCmd,
    cmd_id: FuElanTsWriteCmdId == EnterIap,
    payload: u16be == 0x1234,
}

// Write Flash Key Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsWriteFlashKeyCmd {
    cmd_type: FuElanTsCmdType == WriteCmd,
    cmd_id: FuElanTsWriteCmdId == WriteFlashKey,
    payload: u16be == 0xE15A,
}

// Re-Calibration (REK) Command
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsRekCmd {
    cmd_type: FuElanTsCmdType == WriteCmd,
    cmd_id: FuElanTsWriteCmdId == Rek,
    payload: u16be == 0x0001,
}

// Response structure for Calibration command
#[derive(ParseBytes, Default)]
#[repr(C, packed)]
struct FuStructElanTsCalibrationRsp {
    payload: u32be == 0x66666666,
}

// Boot Code Version Command Structure
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsBcVersionCmd {
    cmd_type: FuElanTsCmdType == ReadCmd,
    cmd_id: FuElanTsReadCmdId == BootCodeVersion,
    payload: u16be == 0x0001,
}

// Boot Code Version Response Structure
#[derive(ParseBytes, Default)]
#[repr(C, packed)]
struct FuStructElanTsBcVersionRsp {
    rsp_type: FuElanTsRspType == ReadCmdRsp,
    payload: [u8; 3],
}

// Firmware ID Command Structure
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsFwIdCmd {
    cmd_type: FuElanTsCmdType == ReadCmd,
    cmd_id: FuElanTsReadCmdId == FwId,
    payload: u16be == 0x0001,
}

// Firmware ID Response Structure
#[derive(ParseBytes, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsFwIdRsp {
    rsp_type: FuElanTsRspType == ReadCmdRsp,
    payload: [u8; 3],
}

// Firmware Version Command Structure
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsFwVersionCmd {
    cmd_type: FuElanTsCmdType == ReadCmd,
    cmd_id: FuElanTsReadCmdId == FwVersion,
    payload: u16be == 0x0001,
}

// Firmware Version Response Structure
#[derive(ParseBytes, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsFwVersionRsp {
    rsp_type: FuElanTsRspType == ReadCmdRsp,
    payload: [u8; 3],
}

// Test-Solution Version Command Structure
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsTestSolutionVersionCmd {
    cmd_type: FuElanTsCmdType == ReadCmd,
    cmd_id: FuElanTsReadCmdId == TestSolutionVersion,
    payload: u16be == 0x0001,
}

// Test-Solution Version Response Structure
#[derive(ParseBytes, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsTestSolutionVersionRsp {
    rsp_type: FuElanTsRspType == ReadCmdRsp,
    payload: [u8; 3],
}

// Enter Test Mode Command Structure
#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructElanTsEnterTestModeCmd {
    payload: u32be == 0x55555555,
}

// Exit Test Mode Command Structure
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
    Ekth53xx = 0x11,
    Ekth63xx_73xx = 0x21,
}

// operating modes used in Show Bulk ROM Data Command
#[derive(ToString)]
#[repr(u8)]
enum FuElanTsShowBulkRomMode {
    BootCode = 0x00,
    MainCode = 0x10,
}

// read rom data command structure (6 bytes)
#[derive(New, Default, Setters)]
#[repr(C, packed)]
struct FuStructElanTsReadRomCmd {
    cmd_type: FuElanTsCmdType == ReadRomDataCmd,
    mem_addr: u16be,
    pad: u16be == 0x0000,
    mode: FuElanTsReadRomCmdMode = Ekth53xx,
}

// read rom data response structure (6 bytes)
#[derive(ParseBytes, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsReadRomRsp {
    header: FuElanTsRspType == ReadRomCmdRsp,
    mem_addr: u16be,
    rom_data: u16be,
    mode: FuElanTsReadRomCmdMode,
}

// show bulk ROM data command (6 Bytes)
#[derive(New, Default, Setters)]
#[repr(C, packed)]
struct FuStructElanTsShowBulkRomDataCmd {
    cmd_type: FuElanTsCmdType == ShowBulkRomDataCmd,
    mode: FuElanTsShowBulkRomMode = MainCode,
    mem_addr: u16be,
    data_size_words: u16be,
}

// show bulk rom data response frame structure
#[derive(ParseBytes, Default, Getters)]
#[repr(C, packed)]
struct FuStructElanTsShowBulkRomRsp {
    header: FuElanTsRspType == ShowBulkRomDataCmdRsp,   // 0x99 (1 byte)
    frame_index: u8,                                    // 0x00, 0x01, ... (1 byte)
    data_size_words: u8,                                // e.g. 0x1E = 30 words (1 byte)
    data: [u8; 60],                                     // fixed trailing buffer (60 bytes)
}
