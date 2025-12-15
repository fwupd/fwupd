/* Copyright 2025 Harris Tai <harris_tai@pixart.com> */
/* Copyright 2025 Micky Hsieh */
/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * =====================================================================
 * tp protocol enums exported to C via rustgen
 * =====================================================================
 */

/* bank ids */
#[repr(u8)]
enum FuPxiTpSystemBank {
    Bank0 = 0x00,
    Bank1 = 0x01,
    Bank2 = 0x02,
    Bank4 = 0x04,
    Bank6 = 0x06,
}

#[repr(u8)]
enum FuPxiTpUserBank {
    Bank0 = 0x00,
    Bank1 = 0x01,
    Bank2 = 0x02,
}

/* system bank 4 (flash engine registers) */
#[repr(u8)]
enum FuPxiTpRegSys4 {
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

/* system bank 6 (sram buffer) */
#[repr(u8)]
enum FuPxiTpRegSys6 {
    SramGainSelect = 0x08,
    SramSelect     = 0x09,
    SramTrigger    = 0x0a,
    SramData       = 0x0b,
    SramChecksum   = 0x0c,
    SramAddr0      = 0x10,
    SramAddr1      = 0x11,
}

/* system bank 1 (reset control registers) */
#[repr(u8)]
enum FuPxiTpRegSys1 {
    ResetKey1 = 0x2c,
    ResetKey2 = 0x2d,
}

/* system bank 2 (update mode) */
#[repr(u8)]
enum FuPxiTpRegSys2 {
    UpdateMode = 0x0d,
}

/* user bank 0 (part id + crc registers) */
#[repr(u8)]
enum FuPxiTpRegUser0 {
    ProxyMode  = 0x56,
    PartId0    = 0x78,
    PartId1    = 0x79,
    CrcCtrl    = 0x82,
    CrcResult0 = 0x84,
    CrcResult1 = 0x85,
    CrcResult2 = 0x86,
    CrcResult3 = 0x87,
}

/*
 * =====================================================================
 * tp enums
 * =====================================================================
 */

#[repr(u8)]
enum FuPxiTpResetMode {
    Application,
    Bootloader,
}

#[repr(u8)]
enum FuPxiTpResetKey1 {
    Suspend = 0xaa,
}

#[repr(u8)]
enum FuPxiTpResetKey2 {
    Regular    = 0xbb,
    Bootloader = 0xcc,
}

#[repr(u8)]
enum FuPxiTpFlashInst {
    Cmd0 = 0x00,
    Cmd1 = 0x01,
}

#[repr(u8)]
enum FuPxiTpFlashExecState {
    Busy    = 0x01,
    Success = 0x00,
}

#[repr(u8)]
enum FuPxiTpFlashWriteEnable {
    Success = 0x02,
}

#[repr(u8)]
enum FuPxiTpFlashStatus {
    Busy = 0x01,
}

#[repr(u32)]
enum FuPxiTpFlashCcr {
    WriteEnable = 0x0000_0106,
    ReadStatus  = 0x0100_0105,
    EraseSector = 0x0000_2520,
    ProgramPage = 0x0100_2502,
}

#[repr(u16)]
enum FuPxiTpPartId {
    Pjp274 = 0x0274,
}

#[repr(u8)]
enum FuPxiTpCrcCtrl {
    FwBank0    = 0x02,
    FwBank1    = 0x10,
    ParamBank0 = 0x04,
    ParamBank1 = 0x20,
    Busy       = 0x01,
}

/* host <-> tf pass-through proxy mode */
#[repr(u8)]
enum FuPxiTpProxyMode {
    Normal   = 0x00,
    TfUpdate = 0x01,
}

/*
 * =====================================================================
 * tf (touch firmware) enums exported to C
 * =====================================================================
 */

/* tf feature report / rmi frame constants */
#[repr(u8)]
enum FuPxiTfFrameConst {
    Preamble      = 0x5a,
    Tail          = 0xa5,
    ExceptionFlag = 0x80,
}

/* tf hid report ids */
#[repr(u8)]
enum FuPxiTfReportId {
    PassThrough = 0xcc,
}

/* tf rmi target address */
#[repr(u8)]
enum FuPxiTfTargetAddr {
    RmiFrame = 0x2c,
}

/* tf rmi function codes (func field) */
#[repr(u8)]
enum FuPxiTfRmiFunc {
    WriteSimple = 0x00,
    WritePacket = 0x04,
    Read        = 0x0b,
}

/* tf function command ids (high-level tf commands) */
#[repr(u16)]
enum FuPxiTfCmd {
    SetUpgradeMode    = 0x0001,
    WriteUpgradeData  = 0x0002,
    ReadUpgradeStatus = 0x0003,
    ReadVersion       = 0x0007,
    TouchControl      = 0x0303,
}

/* tf upgrade mode payload */
#[repr(u8)]
enum FuPxiTfUpgradeMode {
    Exit       = 0x00,
    EnterBoot  = 0x01,
    EraseFlash = 0x02,
}

/* tf touch control */
#[repr(u8)]
enum FuPxiTfTouchControl {
    Enable  = 0x00,
    Disable = 0x01,
}

/* tf firmware version mode */
#[repr(u8)]
enum FuPxiTfFwMode {
    App  = 1,
    Boot = 2,
    Algo = 3,
}

/*
 * =====================================================================
 * tf frame layout / sizes (enum-only, rustgen-exported)
 * =====================================================================
 */

#[repr(u8)]
enum FuPxiTfFrameSize {
    FeatureReportLen = 64,
    CrcBytes         = 1,
    TailBytes        = 1,
}

#[repr(u8)]
enum FuPxiTfFrameOffset {
    CrcStart = 2,
}

#[repr(u8)]
enum FuPxiTfReplyLayout {
    ReplyHdrBytes = 6,
    TrailerBytes  = 2,
}

#[repr(u8)]
enum FuPxiTfPayloadSize {
    Version        = 3,
    DownloadStatus = 3,
}

#[repr(u8)]
enum FuPxiTfLimit {
    MaxPacketDataLen  = 32,
    RomHeaderSkip     = 6,
    RomHeaderCheckEnd = 128,
    RomHeaderZero     = 0x00,
}

#[repr(u16)]
enum FuPxiTfTiming {
    RmiReplyWait        = 10,
    BootloaderEnterWait = 100,
    EraseWait           = 2000,
    DownloadPostWait    = 50,
    AppVersionWait      = 1000,
    DefaultSendInterval = 50,
}

#[repr(u8)]
enum FuPxiTfRetry {
    Times         = 3,
    IntervalMs    = 10,
}

/*
 * =====================================================================
 * tf payload structs
 * =====================================================================
 */

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPxiTfVersionPayload {
    major: u8,
    minor: u8,
    patch: u8,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPxiTfDownloadStatusPayload {
    status: u8,
    packet_number: u16le,
}

/*
 * =====================================================================
 * tf command / reply structs
 * =====================================================================
 */

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPxiTfWriteSimpleCmd {
    report_id:   FuPxiTfReportId   = PassThrough,
    preamble:    FuPxiTfFrameConst = Preamble,
    target_addr: FuPxiTfTargetAddr = RmiFrame,
    func:        FuPxiTfRmiFunc    = WriteSimple,
    addr:        u16le,
    len:         u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPxiTfWritePacketCmd {
    report_id:    FuPxiTfReportId   = PassThrough,
    preamble:     FuPxiTfFrameConst = Preamble,
    target_addr:  FuPxiTfTargetAddr = RmiFrame,
    func:         FuPxiTfRmiFunc    = WritePacket,
    addr:         u16le,
    datalen:      u16le,
    packet_total: u16le,
    packet_index: u16le,
}

#[derive(New, Default)]
#[repr(C, packed)]
struct FuStructPxiTfReadCmd {
    report_id:   FuPxiTfReportId   = PassThrough,
    preamble:    FuPxiTfFrameConst = Preamble,
    target_addr: FuPxiTfTargetAddr = RmiFrame,
    func:        FuPxiTfRmiFunc    = Read,
    addr:        u16le,
    datalen:     u16le,
    reply_len:   u16le,
}

#[derive(Parse, Getters)]
#[repr(C, packed)]
struct FuStructPxiTfReplyHdr {
    report_id:   FuPxiTfReportId,
    preamble:    FuPxiTfFrameConst,
    target_addr: FuPxiTfTargetAddr,
    func:        FuPxiTfRmiFunc,
    datalen:     u16le,
}

/*
 * =====================================================================
 * fw header (FWHD) definitions
 * =====================================================================
 */

#[repr(u16)]
enum FuPxiTpFwHeaderOffset {
    Magic        = 0x0000,
    HeaderLen    = 0x0004,
    SectionsBase = 0x0014,
}

#[derive(ToString)]
#[repr(u8)]
enum FuPxiTpUpdateType {
    General    = 0,
    FwSection  = 1,
    Bootloader = 2,
    Param      = 3,
    TfForce    = 16,
}

/* firmware flags (bitmask) */
#[repr(u32)]
enum FuPxiTpFirmwareFlags {
    None       = 0,
    Valid      = 1 << 0,
    IsExternal = 1 << 1,
}

/* firmware header (parse from bytes) */
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructPxiTpFirmwareHdr {
    magic:         [u8; 4],
    header_len:    u16le,
    header_ver:    u16le,
    file_ver:      u16le,
    ic_part_id:    u16le,
    flash_sectors: u16le,
    file_crc32:    u32le,
    num_sections:  u16le,
}

/* firmware section header (parse from bytes) */
#[derive(Parse)]
#[repr(C, packed)]
struct FuStructPxiTpFirmwareSectionHdr {
    update_type:         FuPxiTpUpdateType,
    update_info:         u8,
    target_flash_start:  u32le,
    internal_file_start: u32le,
    section_length:      u32le,
    section_crc:         u32le,
    shared:              [u8; 12],
    extname:             [u8; 34],
}
