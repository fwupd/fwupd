struct CfuRspGetFirmwareVersion {
    component_cnt: u8
    _reserved: u16le
    flags: u8
}
struct CfuRspGetFirmwareVersionComponent {
    fw_version: u32le
    flags: u8
    component_id: u8
    _vendor_specific: u16le
}
struct CfuRspFirmwareUpdateOffer {
    _reserved1: 3u8
    token: u8
    _reserved2: 4u8
    rr_code: u8
    _reserved3: 3u8
    status: u8
    _reserved3: 3u8
}
struct CfuReqFirmwareUpdateContent {
    flags: u8
    data_length: u8
    seq_number: u16le
    address: u32le
}
struct CfuRspFirmwareUpdateContent {
    seq_number: u16le
    _reserved1: u16le
    status: u8
    _reserved2: 3u8
    _reserved3: 4u8
    _reserved4: 4u8
}
