// Copyright 2025 Harris Tai <harris_tai@pixart.com>
// Copyright 2025 Micky Hsieh
// SPDX-License-Identifier: LGPL-2.1-or-later

// bank ids
#[repr(u8)]
enum FuPixartTpSystemBank {
    Bank0 = 0x00,
    Bank1 = 0x01,
    Bank2 = 0x02,
    Bank4 = 0x04,
    Bank6 = 0x06,
}

#[repr(u8)]
enum FuPixartTpUserBank {
    Bank0 = 0x00,
    Bank1 = 0x01,
    Bank2 = 0x02,
}

// system bank 4 (flash engine registers)
#[repr(u8)]
enum FuPixartTpRegSys4 {
    FlashStatus   = 0x1c,
    SwapFlag      = 0x29,
    FlashInstCmd  = 0x2c,
    FlashBufAddr0 = 0x2e,
    FlashBufAddr1 = 0x2f,
    FlashCcr0     = 0x40,
    FlashCcr1     = 0x41,
    FlashCcr2     = 0x42,
    FlashCcr3     = 0x43,
    FlashDataCnt0 = 0x44,
    FlashDataCnt1 = 0x45,
    FlashAddr0    = 0x48,
    FlashAddr1    = 0x49,
    FlashAddr2    = 0x4a,
    FlashAddr3    = 0x4b,
    FlashExecute  = 0x56,
}

// system bank 6 (sram buffer)
#[repr(u8)]
enum FuPixartTpRegSys6 {
    SramGainSelect = 0x08,
    SramSelect     = 0x09,
    SramTrigger    = 0x0a,
    SramData       = 0x0b,
    SramChecksum   = 0x0c,
    SramAddr0      = 0x10,
    SramAddr1      = 0x11,
}

// system bank 1 (reset control registers)
#[repr(u8)]
enum FuPixartTpRegSys1 {
    ClocksPowerUp   = 0x0d,
    ResetKey1       = 0x2c,
    ResetKey2       = 0x2d,
}

// user bank 0 (part id + crc registers)
#[repr(u8)]
enum FuPixartTpRegUser0 {
    BootStaus  = 0x00,
    RunMode    = 0x16,
    ProxyMode  = 0x56,
    PartId0    = 0x78,
    PartId1    = 0x79,
    CrcCtrl    = 0x82,
    CrcResult0 = 0x84,
    CrcResult1 = 0x85,
    CrcResult2 = 0x86,
    CrcResult3 = 0x87,
}

#[repr(u8)]
enum FuPixartTpBootStatus{
    Rom = 0x8c,
}

#[repr(u8)]
enum FuPixartTpRunMode {
    Auto     = 0x00,
    ForceRun = 0x01,
}

#[repr(u8)]
enum FuPixartTpResetMode {
    Application,
    Bootloader,
}

#[repr(u8)]
enum FuPixartTpResetKey1 {
    Suspend = 0xaa,
}

#[repr(u8)]
enum FuPixartTpResetKey2 {
    Regular    = 0xbb,
    Bootloader = 0xcc,
}

#[repr(u8)]
enum FuPixartTpFlashInst {
    None               = 0,
    Rd2RegBank         = 1 << 0,
    Program            = 1 << 2,
    InternalSramAccess = 1 << 7,
}

#[repr(u8)]
enum FuPixartTpClocksPowerUp {
    CPU = 1 << 1,
}

#[repr(u8)]
enum FuPixartTpFlashExecState {
    Busy    = 0x01,
    Success = 0x00,
}

#[repr(u8)]
enum FuPixartTpFlashWriteEnable {
    Success = 0x02,
}

#[repr(u8)]
enum FuPixartTpFlashStatus {
    Busy = 0x01,
}

#[repr(u32)]
enum FuPixartTpFlashCcr {
    WriteEnable = 0x0000_0106,
    ReadStatus  = 0x0100_0105,
    EraseSector = 0x0000_2520,
    ProgramPage = 0x0100_2502,
}

#[repr(u16)]
enum FuPixartTpPartId {
    Pjp239 = 0x0239,
    Pjp274 = 0x0274,
}

#[repr(u8)]
enum FuPixartTpCrcCtrl {
    FwBank0    = 0x02,
    FwBank1    = 0x10,
    ParamBank0 = 0x04,
    ParamBank1 = 0x20,
    Busy       = 0x01,
}

// host <-> tf pass-through proxy mode
#[repr(u8)]
enum FuPixartTpProxyMode {
    Normal   = 0x00,
    TfUpdate = 0x01,
}

// tf feature report / rmi frame constants
#[repr(u8)]
enum FuPixartTpTfFrameConst {
    Preamble      = 0x5a,
    Tail          = 0xa5,
    ExceptionFlag = 0x80,
}

// tf hid report ids
#[repr(u8)]
enum FuPixartTpTfReportId {
    PassThrough = 0xcc,
}

// tf rmi target address
#[repr(u8)]
enum FuPixartTpTfTargetAddr {
    RmiFrame = 0x2c,
}

// tf rmi function codes (func field)
#[repr(u8)]
enum FuPixartTpTfRmiFunc {
    WriteSimple = 0x00,
    WritePacket = 0x04,
    Read        = 0x0b,
}

// tf function command ids (high-level tf commands)
enum FuPixartTpTfCmd {
    SetUpgradeMode    = 0x0001,
    WriteUpgradeData  = 0x0002,
    ReadUpgradeStatus = 0x0003,
    ReadVersion       = 0x0007,
    TouchControl      = 0x0303,
}

// tf upgrade mode payload
enum FuPixartTpTfUpgradeMode {
    Exit       = 0x00,
    EnterBoot  = 0x01,
    EraseFlash = 0x02,
}

// tf touch control
enum FuPixartTpTfTouchControl {
    Enable  = 0x00,
    Disable = 0x01,
}

// tf firmware version mode
enum FuPixartTpTfFwMode {
    App  = 1,
    Boot = 2,
    Algo = 3,
}

// tf payload structs
#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPixartTpTfVersionPayload {
    major: u8,
    minor: u8,
    patch: u8,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPixartTpTfDownloadStatusPayload {
    status: u8,
    packet_number: u16le,
}

// tf command / reply structs

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpTfWriteSimpleCmd {
    report_id:   FuPixartTpTfReportId   = PassThrough,
    preamble:    FuPixartTpTfFrameConst = Preamble,
    target_addr: FuPixartTpTfTargetAddr = RmiFrame,
    func:        FuPixartTpTfRmiFunc    = WriteSimple,
    addr:        u16le,
    len:         u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpTfWritePacketCmd {
    report_id:    FuPixartTpTfReportId   = PassThrough,
    preamble:     FuPixartTpTfFrameConst = Preamble,
    target_addr:  FuPixartTpTfTargetAddr = RmiFrame,
    func:         FuPixartTpTfRmiFunc    = WritePacket,
    addr:         u16le,
    datalen:      u16le,
    packet_total: u16le,
    packet_index: u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpTfReadCmd {
    report_id:   FuPixartTpTfReportId   = PassThrough,
    preamble:    FuPixartTpTfFrameConst = Preamble,
    target_addr: FuPixartTpTfTargetAddr = RmiFrame,
    func:        FuPixartTpTfRmiFunc    = Read,
    addr:        u16le,
    datalen:     u16le,
    reply_len:   u16le,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPixartTpTfReplyHdr {
    report_id:   FuPixartTpTfReportId,
    preamble:    FuPixartTpTfFrameConst,
    target_addr: FuPixartTpTfTargetAddr,
    func:        FuPixartTpTfRmiFunc,
    datalen:     u16le,
}

#[derive(ToString, FromString)]
#[repr(u8)]
enum FuPixartTpUpdateType {
    General    = 0,
    FwSection  = 1,
    Bootloader = 2,
    Param      = 3,
    TfForce    = 16,
}

#[derive(ToString, FromString)]
#[repr(u32)]
enum FuPixartTpFirmwareFlags {
    None       = 0,
    Valid      = 1 << 0,
    IsExternal = 1 << 1,
}

#[derive(ParseStream, ValidateStream, New, Default)]
#[repr(C, packed)]
struct FuStructPixartTpFirmwareHdr {
    magic:         [char; 4] == "FWHD",
    header_len:    u16le == 0x0218, // for v1.0
    header_ver:    u16le,
    file_ver:      u16le,
    ic_part_id:    u16le,
    flash_sectors: u16le,
    file_crc32:    u32le,
    num_sections:  u16le,
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuStructPixartTpFirmwareSectionHdr {
    update_type:         FuPixartTpUpdateType,
    update_info:         u8,
    target_flash_start:  u32le,
    internal_file_start: u32le,
    section_length:      u32le,
    section_crc:         u32le,
    shared:              [u8; 12],
    extname:             [char; 34],
}
