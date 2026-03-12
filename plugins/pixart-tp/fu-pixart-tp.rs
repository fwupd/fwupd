// Copyright 2025 Harris Tai <harris_tai@pixart.com>
// Copyright 2025 Micky Hsieh
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuPixartTpResetMode {
    Application,
    Bootloader,
}

// bank ids
#[repr(u8)]
enum FuPixartTpSystemBank {
    Bank0 = 0x00,
    Bank1 = 0x01,
    Bank2 = 0x02,
    Bank4 = 0x04,
    Bank6 = 0x06,
    Bank8 = 0x08,
}

#[repr(u8)]
enum FuPixartTpUserBank {
    Bank0 = 0x00,
    Bank1 = 0x01,
    Bank2 = 0x02,
}

// ------------------- Register Definition -----------------// 
// system bank 0 (reset control registers)
#[repr(u8)]
enum FuPixartTpRegSys0 {
    PartId  = 0x78,
}

// system bank 1 (reset control registers)
#[repr(u8)]
enum FuPixartTpRegSys1 {
    ClocksPowerUp   = 0x0d,
    ResetKey1       = 0x2c,
    ResetKey2       = 0x2d,
}

// system bank 4 (flash engine registers)
#[repr(u8)]
enum FuPixartTpRegSys4 {
    FlashStatus   = 0x1c,
    SwapFlag      = 0x29,
    FlashInstCmd  = 0x2c,
    FlashBufAddr  = 0x2e,
    FlashCcr      = 0x40,
    FlashDataCnt  = 0x44,
    FlashAddr     = 0x48,
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
    SramAddr       = 0x10,
    AppReady       = 0x70,
}

// user bank 0 (part id + crc registers)
#[repr(u8)]
enum FuPixartTpRegUser0 {
    BootStaus  = 0x00,
    RunMode    = 0x16,
    ProxyMode  = 0x56,
    CrcCtrl    = 0x82,
    CrcResult  = 0x84,
}
// ------------------- Register Definition (PLP239) -----------------// 
// system bank 0 for PLP239
#[repr(u8)]
enum FuPixartTpRegSys0Plp239 {
    ClockPu         = 0x00,
    ClockPu2        = 0x02,
    ClockPd         = 0x04,
    VersionLow      = 0x16,
    VersionHigh     = 0x18,
    Bank0ProtectKey = 0x7c,
}

// system bank 1 for PLP239
#[repr(u8)]
enum FuPixartTpRegSys1Plp239 {
    ResetKey1       = 0x20,
    ResetKey2       = 0x21,
}

// system bank 4 for PLP239
#[repr(u8)]
enum FuPixartTpRegSys4Plp239 {
    LowLevelProtection  = 0x18,
    MaskCpuAccess       = 0x19,
    FlashCommand        = 0x1a,
    SramOffset          = 0x1c,
    FlashDataCount      = 0x1e,
    FlashAddress        = 0x20,
    FlashWriteDataWord0 = 0x24,      
    FlashInstruction    = 0x2c,
    FlashQuad           = 0x2f,
    DummyBy             = 0x30,
}

// system bank 6 for PLP239
#[repr(u8)]
enum FuPixartTpRegSys6Plp239 {
    FirmwareCrc             = 0x08,
    ParameterCrc            = 0x0c,
    BootStatus              = 0x10,
    ZeroLevelProtectKey     = 0x20,
    ProgramBist             = 0x21,
    OneLevelProtectKey      = 0x22,
    FlashSectorAddress      = 0x23,
    FlashSectorLength       = 0x24,
    SfcCommand              = 0x25,
    SramAccessData          = 0x26,
    FlashControllerStatus   = 0x27,
    HidDescriptorCrcCtrl    = 0x6b,
    HidDescriptorCrc        = 0x6c,
    WatchdogDisable         = 0x7d,
}

// system bank 8 for PLP239
#[repr(u8)]
enum FuPixartTpRegSys8Plp239 {
    HidFirmwareReady    = 0x68,
}

// ------------------- System Bank 1 Key -----------------// 
// 0x0d: ClocksPowerUp
#[repr(u8)]
enum FuPixartTpClocksPowerUp {
    None    = 0,
    Cpu     = 1 << 1,
    Nyq     = 1 << 6,
    NyqF    = 1 << 7,
}

// 0x2c: ResetKey1
#[repr(u8)]
enum FuPixartTpResetKey1 {
    Suspend = 0xaa,
}
// 0x2d: ResetKey2
#[repr(u8)]
enum FuPixartTpResetKey2 {
    Regular    = 0xbb,
    Bootloader = 0xcc,
}

// ------------------- System Bank 4 Key -----------------//
// 0x1c: FlashStatus
#[repr(u8)]
enum FuPixartTpFlashWriteEnable {
    Busy = 0x01,
    Success = 0x02,
}

// 0x2c: FlashInstCmd
#[repr(u8)]
enum FuPixartTpFlashInst {
    None               = 0,
    Rd2RegBank         = 1 << 0,
    Program            = 1 << 2,
    InternalSramAccess = 1 << 7,
}

// 0x40: FlashCcr
#[repr(u32)]
enum FuPixartTpFlashCcr {
    WriteEnable = 0x0000_0106,
    ReadStatus  = 0x0100_0105,
    EraseSector = 0x0000_2520,
    ProgramPage = 0x0100_2502,
}

// 0x56: FlashExecute
#[repr(u8)]
enum FuPixartTpFlashExecState {
    Busy    = 0x01,
    Success = 0x00,
}

// ------------------- User Bank 0 Key -----------------// 
// 0x00: BootStaus
#[repr(u8)]
enum FuPixartTpBootStatus{
    Rom = 0x8c,
}

// 0x16: RunMode
#[repr(u8)]
enum FuPixartTpRunMode {
    Auto     = 0x00,
    ForceRun = 0x01,
}

// 0x78: PartId
#[repr(u16)]
enum FuPixartTpPartId {
    Plp239 = 0x0239,
    Pjp274 = 0x0274,
}

// 0x82: CrcCtrl
#[repr(u8)]
enum FuPixartTpCrcCtrl {
    FwBank0         = 0x02,
    FwBank1         = 0x10,
    ParamBank0      = 0x04,
    ParamBank1      = 0x20,
    HidDescriptor   = 0x0a,
    Busy            = 0x01,
}

// ------------------- System Bank 0 Key (PLP239) -----------------// 
// 0x04: ClockPd
#[repr(u8)]
enum FuPixartTpClocksPowerDisablePlp239 {
    None    = 0,
    Cpu     = 1 << 1,
}

// ------------------- System Bank 4 Key (PLP239) -----------------// 
// 0x18: LowLevelProtection
#[repr(u8)]
enum FuPixartTpLowLevelProtectionKeyPlp239 {
    Lock    = 0x00,
    Unlock  = 0xcc,
}

// 0x19: MaskCpuAccess
#[repr(u8)]
enum FuPixartTpCpuAccessMaskPlp239 {
    Enable    = 0x00,
    Disable   = 0x01,
}

// 0x1a: FlashCommand
#[repr(u8)]
enum FuPixartTpFlashCommandPlp239 {
    // Generic Control
    FlashCmdWrsr    = 0x01, // Write Status, EoN/Winbond
    FlashCmdRdsr    = 0x05, // Read Status, EoN/Winbond
    FlashCmdRdsr2Wb = 0x35, // Read Status Register 2, Winbond
    FlashCmdWren    = 0x06, // Write Enable, EoN/Winbond
    FlashCmdWrdi    = 0x04, // Write Disable, EoN/Winbond

    // Program / Erase
    FlashCmdPp       = 0x02, // Page Program, EoN/Winbond
    FlashCmdQualPpWb = 0x32, // Quad Page Program, Winbond
    FlashCmdSe       = 0x20, // Sector Erase (4K), EoN/Winbond
    FlashCmdHbe      = 0x52, // Half Block Erase (32K), EoN/Winbond
    FlashCmdBe       = 0xd8, // Block Erase (64K), EoN/Winbond
    FlashCmdCe0      = 0x60, // Chip Erase Alt, EoN/Winbond
    FlashCmdCe       = 0xc7, // Chip Erase, EoN/Winbond

    // Identification & Reset
    FlashCmdRdid      = 0x9f, // Read JEDEC ID, EoN/Winbond
    FlashCmdRstenEon  = 0x66, // Reset Enable, EoN
    FlashCmdRstEon    = 0x99, // Reset, EoN
    FlashCmdEqpiEon   = 0x38, // Enable Quad Peripheral Interface, EoN
    FlashCmdRstqioEon = 0xff, // Reset Quad I/O, EoN

    // Read Operations
    FlashCmdRead                 = 0x03, // Read Data
    FlashCmdFastRead             = 0x0b, // Fast Read
    FlashCmdFastReadDualOutput   = 0x3b, // Dual Output Fast Read
    FlashCmdFastReadQualOutputWb = 0x6b, // Quad Output Fast Read, Winbond
    FlashCmdDualIoFastRead       = 0xbb, // Dual I/O Fast Read
    FlashCmdQuadIoFastRead       = 0xeb, // Quad I/O Fast Read

    // Power Management
    FlashCmdPowrDown         = 0xb9, // Power down
    FlashCmdReleasePowerDown = 0xab, // Release power down
}

// 0x2c: FlashInstruction
#[repr(u8)]
enum FuPixartTpFlashInstPlp239 {
    None                    = 0,
    FlashWriteData          = 1 << 0,
    FlashWriteInstruction   = 1 << 1,
    FlashReadData           = 1 << 2,
    FlashReadInstruction    = 1 << 3,
    InternalSramAccess      = 1 << 7,
}

// 0x2f: FlashQuad
#[repr(u8)]
enum FuPixartTpFlashQuad {
    None            = 0,
    EqioEon         = 1 << 0,
    Q4ppWinb        = 1 << 1,
    Q4ppEon         = 1 << 2,
    Q4ppMxic        = 1 << 3,
    QuadEnperfMd    = 1 << 4,
    QuadDualMd      = 1 << 5,
    QuadMd          = 1 << 6,
    FdualoutMd      = 1 << 7,
}

// ------------------- System Bank 6 Key (PLP239) -----------------// 
// 0x10: BootStatus
#[repr(u8)]
enum FuPixartTpBootStatusPlp239{
    None                = 0,
    HwReady             = 1 << 0,
    FwCodePass          = 1 << 1,
    HidPass             = 1 << 2,
    IfbCheckSumPass     = 1 << 3,
    Wdog                = 1 << 4,
    EhiReady            = 1 << 5,
    Error               = 1 << 6,
    NavReady            = 1 << 7,
}

// 0x20: ZeroLevelProtectKey
#[repr(u8)]
enum FuPixartTpZeroLevelProtectKeyPlp239 {
    ProtectKey  = 0xcc,
}

// 0x22: OneLevelProtectKey
#[repr(u8)]
enum FuPixartTpOneLevelProtectKeyPlp239 {
    ProtectKey  = 0xee,
}

// 0x24: FlashSectorLength
#[repr(u8)]
enum FuPixartTpFlashSectorLengthPlp239 {
    KB_4  = 0x00, // 4 KB
}

// 0x25: SfcCommand
#[repr(u8)]
enum FuPixartTpSfcCommandPlp239 {
    SramReadWrite   = 0x11,
    Program         = 0x33,
    Erase           = 0x44,
    Read            = 0x77,
    Finish          = 0xdd,
}

// 0x27: FlashControllerStatus
#[repr(u8)]
enum FuPixartTpFlashControllerStatusPlp239 {
    Finish          = 1 << 0,
    BufferOverFlow  = 1 << 1,
    ProgramOverFlow = 1 << 2,
    BistError       = 1 << 3,
    ProtectError    = 1 << 4,
    HighLevelEnable = 1 << 5,
    Level1Enable    = 1 << 6,
    CpuClockEnable  = 1 << 7,
}

// 0x7d: WatchdogDisable
#[repr(u8)]
enum FuPixartTpWatchDogKeyPlp239 {
    Enable  = 0x00,
    Disable = 0xad,
}

// ------------------- System Bank 8 Key (PLP239) -----------------// 
// 0x68: HidFirmwareReady
#[repr(u8)]
enum FuPixartTpHidFirmwareReadyPlp239 {
    None                = 0,
    HidFirmwareReady    = 1 << 1,
    FirmwareReady       = 1 << 2,
}

// ------------------- TF Haptic Command -----------------// 
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
    General         = 0,
    FwSection       = 1,
    Bootloader      = 2,
    Param           = 3,
    HidDescriptor   = 4,
    TfForce         = 16,
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
