#[derive(ToString)]
#[repr(u8)]
enum CcgxPureHidFwMode {
    Boot,
    Fw1,
    Fw2,
}

#[repr(u8)]
enum CcgxPureHidReportId {
    Info = 0xE0,
    Command = 0xE1,
    Write = 0xE2,
    Read = 0xE3,
    Custom = 0xE4,
}

#[repr(u8)]
enum CcgxPureHidCommand {
    Jump = 0x01,
    Flash = 0x02,
    SetBoot = 0x04,
    Mode = 0x06,
}

#[derive(Parse)]
struct CcgxPureHidFwInfo {
    report_id: CcgxPureHidReportId == Info,
    _reserved_1: u8,
    signature: u16le == 0x5943,
    operating_mode: CcgxPureHidFwMode,
    bootloader_info: u8,
    bootmode_reason: u8,
    _reserved_2: u8,
    silicon_id: u32le,
    bl_version: u32le,
    _bl_version_reserved: [u8; 4],
    image1_version: u32le,
    _image1_version_reserved: [u8; 4],
    image2_version: u32le,
    _image2_version_reserved: [u8; 4],
    image1_row: u32le,
    image2_row: u32le,
    device_uid: [u8; 6],
    _reserved_3: [u8; 10],
}

#[derive(New)]
struct CcgxPureHidCommand {
    report_id: CcgxPureHidReportId == Command,
    cmd: u8,
    opt: u8,
    pad1: u8 = 0x00,
    pad2: u32le = 0xCCCCCCCC,
}

#[derive(New)]
struct CcgxPureHidWriteHdr {
    report_id: CcgxPureHidReportId == Write,
    pd_resp: u8,
    addr: u16le,
    data: [u8; 128],
}
