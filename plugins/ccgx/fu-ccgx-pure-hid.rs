#[derive(ToString)]
#[repr(u8)]
enum CcgxPureHidFwMode {
    Boot,
    Fw1,
    Fw2,
}

#[derive(Parse)]
struct CcgxPureHidFwInfo {
    report_id: u8: const=0xE0,
    _reserved_1: u8,
    signature: u16le: const=0x5943,
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

#[repr(u8)]
enum FuCcgxPureHidReportId {
    Info = 0xE0,
    Command = 0xE1,
    Write = 0xE2,
    Read = 0xE3,
    Custom = 0xE4,
}

#[derive(New)]
struct CcgxPureHidWriteHdr {
    report_id: FuCcgxPureHidReportId: const=0xE2,
    pd_resp: u8,
    addr: u16le,
    data: [u8; 128],
}
