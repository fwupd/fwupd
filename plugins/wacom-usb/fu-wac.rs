struct WtaBlockHeader {
    block_start: u32le
    block_size: u32le
}
enum WacReportId {
    FwDescriptor              = 0xCB, // GET_FEATURE
    SwitchToFlashLoader       = 0xCC, // SET_FEATURE
    QuitAndReset              = 0xCD, // SET_FEATURE
    ReadBlockData             = 0xD1, // GET_FEATURE
    WriteBlock                = 0xD2, // SET_FEATURE
    EraseBlock                = 0xD3, // SET_FEATURE
    SetReadAddress            = 0xD4, // GET_FEATURE
    GetStatus                 = 0xD5, // GET_FEATURE
    UpdateReset               = 0xD6, // SET_FEATURE
    WriteWord                 = 0xD7, // SET_FEATURE
    GetParameters             = 0xD8, // GET_FEATURE
    GetFlashDescriptor        = 0xD9, // GET_FEATURE
    GetChecksums              = 0xDA, // GET_FEATURE
    SetChecksumForBlock       = 0xDB, // SET_FEATURE
    CalculateChecksumForBlock = 0xDC, // SET_FEATURE
    WriteChecksumTable        = 0xDE, // SET_FEATURE
    GetCurrentFirmwareIdx     = 0xE2, // GET_FEATURE
    Module                    = 0xE4,
}
enum WacModuleFwType {
    Touch         = 0x00,
    Bluetooth     = 0x01,
    EmrCorrection = 0x02,
    BluetoothHid  = 0x03,
    Scaler        = 0x04,
    BluetoothId6  = 0x06,
    TouchId7      = 0x07,
    Main          = 0x3F,
}
enum WacModuleCommand {
    Start = 0x01,
    Data  = 0x02,
    End   = 0x03,
}
enum WacModuleStatus {
    Ok,
    Busy,
    ErrCrc,
    ErrCmd,
    ErrHwAccessFail,
    ErrFlashNoSupport,
    ErrModeWrong,
    ErrMpuNoSupport,
    ErrVersionNoSupport,
    ErrErase,
    ErrWrite,
    ErrExit,
    Err,
    ErrInvalidOp,
    ErrWrongImage,
}
