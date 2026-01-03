#[repr(u8)]
enum FuLenovoHidDataHeader {
    TargetStatus = 0x01,
    DataSize = 0x02,
    CommandClass = 0x03,
    CommandId = 0x04,
    FlagProfile = 0x05,
    Reserved = 0x06,
    PayloadBase = 0x07,
}

#[repr(u8)]
enum FuLenovoHidCommandClass {
    DfuClass = 0x09,
}

#[repr(u8)]
enum FuLenovoHidCommandId {
    DfuAttribute = 0x01,
    DfuPrepare = 0x02,
    DfuFile = 0x03,
    DfuCrc = 0x04,
    DfuExit = 0x05,
    DfuEntry = 0x06,
}

#[repr(u8)]
enum FuLenovoHidCmdDir {
    CmdSet = 0x00,
    CmdGet = 0x01,
}