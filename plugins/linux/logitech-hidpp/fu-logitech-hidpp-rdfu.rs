// Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// the same as FuLogitechHidppReportId, need for the header
#[repr(u8)]
enum FuLogitechHidppRdfuReportId {
    Short = 0x10,
    Long = 0x11,
    VeryLong = 0x12,
    Notification = 0x01,
}


#[repr(u8)]
enum FuLogitechHidppRdfuState {
    NotStarted,
    Transfer,
    Wait, // waiting the event from the device
    ResumeDfu, // for soft recover
    Apply,
}

#[repr(u8)]
enum FuLogitechHidppRdfuFunc {
    GetCapabilities = 0,
    StartDfu = 1 << 4,
    GetDfuStatus = 2 << 4,
    ApplyDfu = 3 << 4,
    TransferDfuData = 4 << 4,
}

// For both status and error codes
#[derive(ToString)]
#[repr(u8)]
enum FuLogitechHidppRdfuResponseCode {
// Status Codes
    DfuNotStarted = 0x01,
    DataTransferReady = 0x02,
    DataTransferWait = 0x03,
    DfuTransferComplete = 0x04,
    DfuApplyPending = 0x05,
    DfuTransferPktAck = 0x06,
    DfuAbort = 0x07,
// Error Codes
    InvalidMagicString = 0x80,
    InvalidFwEntity = 0x81,
    DeviceBusy = 0x82,
    DeviceOperationFailure = 0x83,
    NotSupported = 0x84,
    DfuStateError = 0x85,
    InvalidBlock = 0x86,
    GenericError = 0xFF,
}

#[derive(Default, Parse)]
struct FuStructLogitechHidppRdfuResponse {
    report_id: FuLogitechHidppRdfuReportId == Long,
    device_id: u8,
    sub_id: u8,
    function_id: FuLogitechHidppRdfuFunc,
    fw_entity: u8,
    status_code: FuLogitechHidppRdfuResponseCode,
    parameters: [u8; 14],
}

#[repr(u8)]
enum FuLogitechHidppRdfuCapabilities {
    ResumableDfuBit = 1,
    DeferrableDfuBit = 1 << 1,
    ForcibleDfuBit = 1 << 2,
}

#[derive(New, Default)]
struct FuStructLogitechHidppRdfuGetCapabilities {
    report_id: FuLogitechHidppRdfuReportId == Long,
    device_id: u8,
    sub_id: u8,
    function_id: FuLogitechHidppRdfuFunc == GetCapabilities,
    data: [u8; 16],
}

#[derive(Default, Parse)]
struct FuStructLogitechHidppRdfuCapabilities {
    report_id: FuLogitechHidppRdfuReportId == Long,
    device_id: u8,
    sub_id: u8,
    function_id: FuLogitechHidppRdfuFunc == GetCapabilities,
    capabilities: u8,
    data: [u8; 15],
}

#[derive(New, Default)]
struct FuStructLogitechHidppRdfuGetDfuStatus {
    report_id: FuLogitechHidppRdfuReportId == Long,
    device_id: u8,
    sub_id: u8,
    function_id: FuLogitechHidppRdfuFunc == GetDfuStatus,
    fw_entity: u8,
}

#[derive(New, Default)]
struct FuStructLogitechHidppRdfuStartDfu {
    report_id: FuLogitechHidppRdfuReportId == Long,
    device_id: u8,
    sub_id: u8,
    function_id: FuLogitechHidppRdfuFunc == StartDfu,
    fw_entity: u8,
    magic: [u8; 10],
}

#[derive(Default, Parse)]
struct FuStructLogitechHidppRdfuStartDfuResponse {
    report_id: FuLogitechHidppRdfuReportId == Long,
    device_id: u8,
    sub_id: u8,
    function_id: FuLogitechHidppRdfuFunc == StartDfu,
    fw_entity: u8,
    status_code: FuLogitechHidppRdfuResponseCode,
    status_params: u8,
    additional_status_params: u8,
}

#[derive(New, Default)]
struct FuStructLogitechHidppRdfuTransferDfuData {
    report_id: FuLogitechHidppRdfuReportId == Long,
    device_id: u8,
    sub_id: u8,
    function_id: FuLogitechHidppRdfuFunc == TransferDfuData,
    data: [u8; 16],
}


#[derive(Default, Parse)]
struct FuStructLogitechHidppRdfuDataTransferReady {
    status_code: FuLogitechHidppRdfuResponseCode == DataTransferReady,
    block_id: u16be,
}

#[derive(Default, Parse)]
struct FuStructLogitechHidppRdfuDataTransferWait {
    status_code: FuLogitechHidppRdfuResponseCode == DataTransferWait,
    delay_ms: u16be,
}

#[derive(Default, Parse)]
struct FuStructLogitechHidppRdfuDfuTransferPktAck {
    status_code: FuLogitechHidppRdfuResponseCode == DfuTransferPktAck,
    pkt_number: u32be,
}

#[repr(u8)]
enum FuLogitechHidppRdfuApplyFlags {
    DeferDfuBit = 1,
    ForceDfuBit = 1 << 1,
}

#[derive(New, Default)]
struct FuStructLogitechHidppRdfuApplyDfu {
    report_id: FuLogitechHidppRdfuReportId == Long,
    device_id: u8,
    sub_id: u8,
    function_id: FuLogitechHidppRdfuFunc == ApplyDfu,
    fw_entity: u8,
    flags: u8,
}
