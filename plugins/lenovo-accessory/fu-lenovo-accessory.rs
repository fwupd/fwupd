// Copyright 2026 Yuchao Li <liyc44@lenovo.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuLenovoAccessoryCommandClass {
    DeviceInformation = 0x00,
    DfuClass = 0x09,
}

#[repr(u8)]
enum FuLenovoAccessoryInfoId {
    FirmwareVersion = 0x01,
    DeviceMode = 0x04,
    WirelessPairList = 0x13,
}

#[repr(u8)]
enum FuLenovoAccessoryDfuId {
    DfuAttribute = 0x01,
    DfuPrepare = 0x02,
    DfuFile = 0x03,
    DfuCrc = 0x04,
    DfuExit = 0x05,
    DfuEntry = 0x06,
}

#[repr(u8)]
enum FuLenovoAccessoryCmdDir {
    CmdSet = 0x00,
    CmdGet = 0x01,
}

#[repr(u8)]
enum FuLenovoAccessoryPairSlotStatus {
    NeverPaired = 0x00,
    Disconnected = 0x01,
    Connected = 0x02,
}

#[repr(u8)]
enum FuLenovoAccessoryPairListOpCode {
    GetSupportInfo = 0x00,
    GetSlotInfo = 0x01,
    DeleteSlot = 0x02,
    GetSlotInfoV2 = 0x03,
}

#[repr(u8)]
enum FuLenovoAccessoryStatus {
    NewCommand = 0x00,
    CommandBusy = 0x01,
    CommandSuccessful = 0x02,
    CommandFailure = 0x03,
    CommandTimeOut = 0x04,
    CommandNotSupport = 0x05,
}

#[repr(u8)]
enum FuLenovoAccessoryDeviceMode {
    NormalMode = 0x00,
    DriverMode = 0x01,
    DfuMode = 0x02,
}

#[repr(u8)]
enum FuLenovoAccessoryDfuFileType {
    HexFile = 0x00,
    BinFile = 0x01,
}

#[repr(u8)]
enum FuLenovoAccessoryDfuExitCode {
    DfuSuccess = 0x00,
    Abort = 0x01,
}

// asynchronous notifications pushed by the dongle over the interface-2
// interrupt-IN endpoint (input reports, no command/response handshake)
#[repr(u8)]
enum FuLenovoAccessoryNotifyEvent {
    WirelessConnectStatusChange = 0x0B,
    BtHostMsg = 0x0D,
}

#[repr(u8)]
enum FuLenovoAccessoryNotifyConnectStatus {
    Disconnected = 0x00,
    Connected = 0x01,
}

#[derive(Parse, Default)]
#[repr(C, packed)]
struct FuStructLenovoAccessoryNotify {
    report_id: u8 == 0x04,
    ntf_type: u8 == 0x02,
    event: FuLenovoAccessoryNotifyEvent,
    // payload interpretation depends on `event`; for
    // WirelessConnectStatusChange this is the connect status followed by the
    // wireless slot number
    connect_status: FuLenovoAccessoryNotifyConnectStatus,
    slot: u8,
}

#[derive(New, Parse, Default)]
#[repr(C, packed)]
struct FuStructLenovoAccessoryCmd {
    target_status: u8,
    data_size: u8,
    command_class: u8,
    command_id: u8,
    flag_profile: u8,
    reserve: u8,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructLenovoDfuFwReq {
    cmd: FuStructLenovoAccessoryCmd,
    file_type: FuLenovoAccessoryDfuFileType,
    offset_address: u32be,
    data: [u8; 32],
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructLenovoDfuExitReq {
    cmd: FuStructLenovoAccessoryCmd,
    exit_code: FuLenovoAccessoryDfuExitCode,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLenovoDfuAttributeRsp {
    major_ver: u8,
    minor_ver: u8,
    product_pid: u16be,
    processor_id: u8,
    app_max_size: u32be,
    page_size: u32be,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLenovoDfuCrcRsp {
    crc32: u32be,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructLenovoDfuPrepareReq {
    cmd: FuStructLenovoAccessoryCmd,
    file_type: FuLenovoAccessoryDfuFileType,
    start_address: u32be,
    end_address: u32be,
    crc32: u32be,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructLenovoDevicemodeReq {
    cmd: FuStructLenovoAccessoryCmd,
    mode: FuLenovoAccessoryDeviceMode,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLenovoDevicemodeRsp {
    mode: FuLenovoAccessoryDeviceMode,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLenovoFwVersionRsp {
    major: u8,
    minor: u8,
    internal: u8,
}

#[derive(New)]
#[repr(C, packed)]
struct FuStructLenovoAccessoryPairListReq {
    cmd: FuStructLenovoAccessoryCmd,
    op_code: FuLenovoAccessoryPairListOpCode,
    target_slot: u8,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLenovoAccessoryPairSupportInfoRsp {
    op_code: FuLenovoAccessoryPairListOpCode,
    max_slot_num: u8,
    slot_status: [u8; 8],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructLenovoAccessoryPairSlotInfoV2Rsp {
    op_code: FuLenovoAccessoryPairListOpCode,
    target_slot: u8,
    pid: u16be,
    mac_addr: [u8; 6],
    bt_name: [char; 48],
}
