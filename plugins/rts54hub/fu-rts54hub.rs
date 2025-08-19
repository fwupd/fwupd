
#[repr(u8)]
enum FuRts54HubMergeInfoDdcciOpcode {
        Communication = 0x11,
        DdcciToDebug = 0x55,
        First = 0x77,
        GetVersion = 0x99,
        SetVersion = 0xBB,
}
