// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuLenovoDockSignType {
    None = 0x00,
    Rsa2048 = 0x01,
    Rsa3072 = 0x02,
    Ecc256 = 0x10,
    Ecc384 = 0x11,
}

#[derive(ToString)]
#[repr(u8)]
enum FuLenovoDockComponentId {
    None = 0x00,
    Dmc = 0x01,
    Dp = 0x02,
    Pd = 0x03,
    Usb3 = 0x04,
    Usb4 = 0x05,
    Osd = 0x06,
    Dbg = 0x07,
    Usage = 0xFF,
}

#[derive(Parse, ParseStream, Default, ToString, Setters)]
#[repr(C, packed)]
struct FuStructLenovoDockUsage {
    total_number: u8,
    major_version: u8 = 0x00,
    minor_version: u8 = 0x01,
    dsa: FuLenovoDockSignType = None,
    iot_flag: u8,
    composite_version: u24be,
    pid: u16be = 0x111E,
    crc32: u32le,
    reserved: [u8; 18],
    // items: [FuStructLenovoDockUsageItem; total_number],
};

#[repr(u8)]
enum FuLenovoDockUsageItemFlag {
    None,
    DoUpdate,
}

#[derive(Parse, ParseStream, Default, ToString, Setters)]
#[repr(C, packed)]
struct FuStructLenovoDockUsageItem {
    address: u32le,
    max_size: u32le,
    current_version: u32le,
    target_version: u32le,
    target_size: u32le,
    target_crc32: u32le,
    component_id: FuLenovoDockComponentId,
    flag: FuLenovoDockUsageItemFlag,
    reserved: [u8; 6],
};

#[derive(ToString)]
#[repr(u8)]
enum FuLenovoDockStatus {
    Default = 0x00,
    Busy = 0x01,
    Success = 0x02,
    Failure = 0x03,
    Timeout = 0x04,
    NotSupport = 0x05,
}

#[repr(u8)]
enum FuLenovoDockCmdClass {
    DeviceInformation = 0x00,
    Dfu = 0x09,
    Test = 0x0A,
    ExternalFlash = 0x0C,
    Dock = 0x0E,
}

#[repr(u8)]
enum FuLenovoDockCmdId {
    SetHardwareVersion = 0x02,
    SetSerialNumber = 0x03,
    SetDeviceMode = 0x04,
    SetDeviceEdition = 0x06,
    SetDeviceName = 0x08,
    SetDeviceReset = 0x09,
    SetDeviceUuid = 0x14,
    GetCommandSupportList = 0x80,
    GetFirmwareVersion = 0x81,
    GetHardwareVersion = 0x82,
    GetSerialNumber = 0x83,
    GetDeviceMode = 0x84,
    GetDeviceEdition = 0x86,
    GetDeviceName = 0x88,
    GetDeviceUuid = 0x9,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockGenericRes {
    status: FuLenovoDockStatus,
}

#[derive(Default, New)]
struct FuStructLenovoDockGetVersionReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 == 0x02,
    cmd_class: FuLenovoDockCmdClass == DeviceInformation,
    cmd_id: FuLenovoDockCmdId == GetFirmwareVersion,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockGetVersionRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x03,
    cmd_class: FuLenovoDockCmdClass == DeviceInformation,
    cmd_id: FuLenovoDockCmdId == GetFirmwareVersion,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
    version_major: u8,
    version_minor: u8,
    version_micro: u8,
}

#[repr(u8)]
enum FuLenovoDockFlashCmd {
    SetFlashIdUsageInformation = 0x03,
    SetFlashMemoryAccess = 0x04,
    GetSupportList = 0x80,
    GetFlashIdList = 0x81,
    GetFlashAttribute = 0x82,
    GetFlashIdUsageInformation = 0x83,
    GetFlashMemoryAccess = 0x84,
    GetFlashMemorySelfVerify = 0x85,
}

#[repr(u8)]
enum FuLenovoDockExternalDockCmd {
    SetDockPortCtrl = 0x03,
    SetDockFanCtrl = 0x05,
    SetDockIoTConfigure = 0x06,
    SetDockUsbContainerId = 0x07,
    SetDockLanMacAddress = 0x08,
    SetDockAuxLog = 0x09,
    SetDockFirmwareUpgradeCtrl = 0x0A,
    GetCmdSupportList = 0x80,
    GetDockAttribute = 0x81,
    GetDockPortStatus = 0x82,
    GetDockPortCtrl = 0x83,
    GetDockPortConnectedDeviceInformation = 0x84,
    GetDockDockFanCtrl = 0x85,
    GetDockIoTConfigure = 0x86,
    GetDockUsbContainerId = 0x87,
    GetDockLanMacAddress = 0x88,
    GetDockAuxLog = 0x89,
    GetDockFirmwareUpgradeCtrl = 0x8A,
}

#[repr(u8)]
enum FuLenovoDockFlashIdPurpose {
    Common,
    ApplicationData,
    ImageData,
    Firmware,
}

#[repr(u8)]
enum FuLenovoDockFlashMemoryAccessCmd {
    AcccessCtrl,
    Erase,
    Program,
    Read,
    DockErase,
    DockProgram,
    DockRead,
    DockEraseWithAddress,
    DockProgramWithAddress,
    DockReadWithAddress,
}

#[repr(u8)]
enum FuLenovoDockFlashMemoryAccessCtrl {
    Release,
    Request,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashSetAccessReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 == 0x02,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == SetFlashMemoryAccess,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
    cmd: FuLenovoDockFlashMemoryAccessCmd == AcccessCtrl,
    ctrl: FuLenovoDockFlashMemoryAccessCtrl,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashSetAccessRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x00,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == SetFlashMemoryAccess,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashGetAttrsReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 == 0x02,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashAttribute,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashGetAttrsRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x00,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashAttribute,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
    component_id_again: u8 == 0xFF,
    purpose: FuLenovoDockFlashIdPurpose,
    storage_size: u32le,
    erase_size: u16le,
    program_size: u16le,
}

#[repr(u8)]
enum FuLenovoDockFlashMemorySelfVerifyType {
    Signature,
    Crc,
}

#[repr(u8)]
enum FuLenovoDockFlashVerifySignatureResult {
    Fail,
    Pass,
}

#[repr(u8)]
enum FuLenovoDockDockFwCtrlUpgradeStatus {
    NonLock,
    Locked,
}

#[repr(u8)]
enum FuLenovoDockDockFwCtrlUpgradePhaseCtrl {
    Na,
    InPhase1,
    Unplug,
    NonUnplug,
    WaitForTimer,
}

#[derive(Default, New)]
struct FuStructLenovoDockDfuControlReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 == 0x02,
    cmd_class: FuLenovoDockCmdClass == Dock,
    cmd_id: FuLenovoDockExternalDockCmd == SetDockFirmwareUpgradeCtrl,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
    upgrade_status: FuLenovoDockDockFwCtrlUpgradeStatus,
    ctrl: FuLenovoDockDockFwCtrlUpgradePhaseCtrl,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockDfuControlRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x00,
    cmd_class: FuLenovoDockCmdClass == Dock,
    cmd_id: FuLenovoDockExternalDockCmd == SetDockFirmwareUpgradeCtrl,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockGetComponentIdListReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 == 0x00,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashIdList,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockGetComponentIdListRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x01,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashIdList,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
    total: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashReadReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 == 0x08,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashMemoryAccess,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
    memory_access_cmd: FuLenovoDockFlashMemoryAccessCmd == DockReadWithAddress,
    _unknown: u8,
    size: u16le,
    addr: u32le,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashReadRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x06, // this should be 262, but that's more than u8::MAX...
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashMemoryAccess,
    component_id: FuLenovoDockComponentId == None,
    _reserved: u8,
    _size: u16le,
    _addr: u32le,
    data: [u8; 256],
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashEraseReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 == 0x08,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == SetFlashMemoryAccess,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
    memory_access_cmd: FuLenovoDockFlashMemoryAccessCmd == DockEraseWithAddress,
    _unknown: u8,
    size: u16le,
    addr: u32le,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashEraseRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x0,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == SetFlashMemoryAccess,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashProgramReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == SetFlashMemoryAccess,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
    memory_access_cmd: FuLenovoDockFlashMemoryAccessCmd == DockProgramWithAddress,
    _unknown: u8,
    size: u16le,
    addr: u32le,
    // data: [u8; ProgramSize]
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashProgramRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x0,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == SetFlashMemoryAccess,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashVerifyCrcReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 = 0x01,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashMemorySelfVerify,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
    type: FuLenovoDockFlashMemorySelfVerifyType == Crc,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashVerifyCrcRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x4,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashMemorySelfVerify,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
    crc: u32le,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashVerifySignatureReq {
    status: FuLenovoDockStatus == Default,
    datasz: u8 = 0x01,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashMemorySelfVerify,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
    type: FuLenovoDockFlashMemorySelfVerifyType == Signature,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashVerifySignatureRes {
    status: FuLenovoDockStatus == Success,
    datasz: u8 == 0x4,
    cmd_class: FuLenovoDockCmdClass == ExternalFlash,
    cmd_id: FuLenovoDockFlashCmd == GetFlashMemorySelfVerify,
    component_id: FuLenovoDockComponentId,
    _reserved: u8,
    result: FuLenovoDockFlashVerifySignatureResult == Pass,
}