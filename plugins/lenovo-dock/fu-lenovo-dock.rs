// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuLenovoDockSignType {
    Unsigned = 0x00,
    Rsa2048 = 0x01,
    Rsa3072 = 0x02,
    Ecc256 = 0x10,
    Ecc384 = 0x11,
}

#[derive(ToString)]
#[repr(u8)]
enum FuLenovoDockFlashId {
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

#[derive(Parse, ParseStream, Default, ToString, New)]
#[repr(C, packed)]
struct FuStructLenovoDockUsage {
	total_number: u8,
	major_version: u8 = 0x00,
	minor_version: u8 = 0x01,
	dsa: FuLenovoDockSignType = Unsigned,
	iot_flag: u8,
	composite_version: u24be,
	pid: u16be = 0x111E,
	crc32: u32le,
    reserved: [u8; 18],
	// items: [FuStructLenovoDockUsageItem; total_number],
};

#[repr(u8)]
enum FuStructLenovoDockUsageItemFlag {
    None,
    DoUpdate,
}

#[derive(Parse, ParseStream, Default, ToString)]
#[repr(C, packed)]
struct FuStructLenovoDockUsageItem {
	physical_address: u32le,
	max_size: u32le,
	current_version: u32le,
	target_version: u32le,
	target_size: u32le,
	target_crc32: u32le,
	flash_id: FuLenovoDockFlashId,
	flag: FuStructLenovoDockUsageItemFlag,
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
enum FuLenovoDockClassId {
	DeviceInformation = 0x00,
	Dfu = 0x09,
	Test = 0x0A,
	ExternalFlash = 0x0C,
	Dock = 0x0E,
}

#[repr(u8)]
enum FuLenovoDockDeviceInformationCmd {
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
    bufsz: u8 == 0x02,
    cmd_class: FuLenovoDockClassId == DeviceInformation,
	cmd_id: FuLenovoDockDeviceInformationCmd == GetFirmwareVersion,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockGetVersionRes {
    status: FuLenovoDockStatus == Success,
    bufsz: u8 == 0x03,
    cmd_class: FuLenovoDockClassId == DeviceInformation,
	cmd_id: FuLenovoDockDeviceInformationCmd == GetFirmwareVersion,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
    version_major: u8,
    version_minor: u8,
    version_micro: u8,
}

#[repr(u8)]
enum FuLenovoDockExternalFlashCmd {
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
enum FuLenovoDockExternalFlashIdPurpose {
    Common,
    ApplicationData,
    ImageData,
    FirmwareFile,
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
    bufsz: u8 == 0x02,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
    cmd: FuLenovoDockFlashMemoryAccessCmd == AcccessCtrl,
    ctrl: FuLenovoDockFlashMemoryAccessCtrl,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashSetAccessRes {
    status: FuLenovoDockStatus == Success,
    bufsz: u8 == 0x00,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashGetAttrsReq {
    status: FuLenovoDockStatus == Default,
    bufsz: u8 == 0x02,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == GetFlashAttribute,
    flash_id: FuLenovoDockFlashId,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashGetAttrsRes {
    status: FuLenovoDockStatus == Success,
    bufsz: u8 == 0x00,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == GetFlashAttribute,
    flash_id: FuLenovoDockFlashId,
    _reserved: u8,
    flash_id_again: u8 == 0xFF,
    purpose: FuLenovoDockExternalFlashIdPurpose,
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
enum FuLenovoDockFlashVerifyResult {
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
    bufsz: u8 == 0x02,
    cmd_class: FuLenovoDockClassId == Dock,
	cmd_id: FuLenovoDockExternalDockCmd == SetDockFirmwareUpgradeCtrl,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
    upgrade_status: FuLenovoDockDockFwCtrlUpgradeStatus,
    ctrl: FuLenovoDockDockFwCtrlUpgradePhaseCtrl,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockDfuControlRes {
    status: FuLenovoDockStatus == Success,
    bufsz: u8 == 0x00,
    cmd_class: FuLenovoDockClassId == Dock,
	cmd_id: FuLenovoDockExternalDockCmd == SetDockFirmwareUpgradeCtrl,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockGetFlashIdListReq {
    status: FuLenovoDockStatus == Default,
    bufsz: u8 == 0x00,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == GetFlashIdList,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockGetFlashIdListRes {
    status: FuLenovoDockStatus == Success,
    bufsz: u8 == 0x01,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == GetFlashIdList,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
    total: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashReadReq {
    status: FuLenovoDockStatus == Default,
    bufsz: u8 == 0x08,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == GetFlashMemoryAccess,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
    memory_access_cmd: FuLenovoDockFlashMemoryAccessCmd == DockReadWithAddress,
    _unknown: u8,
    size: u16le,
    addr: u32le,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashReadRes {
    status: FuLenovoDockStatus == Success,
    bufsz: u8 == 0x06, // this should be 262, but that's more than u8::MAX...
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == GetFlashMemoryAccess,
    flash_id: FuLenovoDockFlashId == None,
    _reserved: u8,
    _size: u16le,
    _addr: u32le,
    data: [u8; 256],
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashEraseReq {
    status: FuLenovoDockStatus == Default,
    bufsz: u8 == 0x08,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoDockFlashId,
    _reserved: u8,
    memory_access_cmd: FuLenovoDockFlashMemoryAccessCmd == DockEraseWithAddress,
    _unknown: u8,
    size: u16le,
    addr: u32le,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashEraseRes {
    status: FuLenovoDockStatus == Success,
    bufsz: u8 == 0x0,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoDockFlashId,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashProgramReq {
    status: FuLenovoDockStatus == Default,
    bufsz: u8,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoDockFlashId,
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
    bufsz: u8 == 0x0,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoDockFlashId,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoDockFlashVerifyReq {
    status: FuLenovoDockStatus == Default,
    bufsz: u8 = 0x01,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == GetFlashMemorySelfVerify,
    flash_id: FuLenovoDockFlashId,
    _reserved: u8,
    type: FuLenovoDockFlashMemorySelfVerifyType == Crc,
}

#[derive(Default, Parse)]
struct FuStructLenovoDockFlashVerifyRes {
    status: FuLenovoDockStatus == Success,
    bufsz: u8 == 0x4,
    cmd_class: FuLenovoDockClassId == ExternalFlash,
	cmd_id: FuLenovoDockExternalFlashCmd == GetFlashMemorySelfVerify,
    flash_id: FuLenovoDockFlashId,
    _reserved: u8,
    crc: u32le,
}