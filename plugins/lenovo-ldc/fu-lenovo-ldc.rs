// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuLenovoLdcSignType {
    Unsigned,
    Rsa2048,
    Rsa3072,
    Ecc256,
    Ecc384,
}

#[repr(u8)]
enum FuLenovoLdcFlashId {
    None = 0x00,
    Usage = 0xFF,
}

#[derive(Parse, Default, ToString)]
#[repr(C, packed)]
struct FuStructLenovoLdcUsageInformation {
	total_number: u8,
	major_version: u8,
	minor_version: u8,
	dsa: FuLenovoLdcSignType,
	iot_update_flag: u8,
	composite_fw_version: u32le,
	dock_pid: u16le == 0x111E,
	crc32: u32le,
	// items: [FuStructLenovoLdcUsageInformationItem; total_number],
};

#[repr(u8)]
enum FuStructLenovoLdcUsageInformationItemFlag {
    None,
    DoUpdate,
}

#[derive(Parse, Default, ToString)]
#[repr(C, packed)]
struct FuStructLenovoLdcUsageInformationItem {
	physical_address: u32le,
	max_size: u32le,
	current_fw_version: u32le,
	target_fw_version: u32le,
	target_fw_file_size: u32le,
	target_fw_file_crc32: u32le,
	component_id: u8,
	flag: FuStructLenovoLdcUsageInformationItemFlag,
    reserved: [u8; 6],
};

#[derive(ToString)]
#[repr(u8)]
enum FuLenovoLdcTargetStatus {
	CommandDefault = 0x00,
	CommandBusy = 0x01,
	CommandSuccess = 0x02,
	CommandFaliure = 0x03,
	CommandTimeout = 0x04,
	CommandNotSupport = 0x05,
}

#[repr(u8)]
enum FuLenovoLdcClassId {
	DeviceInformation = 0x00,
	Dfu = 0x09,
	Test = 0x0A,
	ExternalFlash = 0x0C,
	Dock = 0x0E,
}

#[repr(u8)]
enum FuLenovoLdcDeviceInformationCmd {
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
struct FuStructLenovoLdcGenericRes {
    target_status: FuLenovoLdcTargetStatus,
}

#[derive(Default, New)]
struct FuStructLenovoLdcGetCompositeVersionReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8 == 0x02,
    cmd_class: FuLenovoLdcClassId == DeviceInformation,
	cmd_id: FuLenovoLdcDeviceInformationCmd == GetFirmwareVersion,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcGetCompositeVersionRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 0x03,
    cmd_class: FuLenovoLdcClassId == DeviceInformation,
	cmd_id: FuLenovoLdcDeviceInformationCmd == GetFirmwareVersion,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
    version_major: u8,
    version_minor: u8,
    version_micro: u8,
}

#[repr(u8)]
enum FuLenovoLdcExternalFlashCmd {
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
enum FuLenovoLdcExternalDockCmd {
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
enum FuLenovoLdcExternalFlashIdPurpose {
    Common,
    ApplicationData,
    ImageData,
    FirmwareFile,
}

#[repr(u8)]
enum FuLenovoLdcFlashMemoryAccessCmd {
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
enum FuLenovoLdcFlashMemoryAccessCtrl {
    Release,
    Request,
}

#[derive(Default, New)]
struct FuStructLenovoLdcSetFlashMemoryAccessReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8 == 0x02,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
    status: FuLenovoLdcFlashMemoryAccessCmd == AcccessCtrl,
    ctrl: FuLenovoLdcFlashMemoryAccessCtrl,
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcSetFlashMemoryAccessRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 0x00,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoLdcGetFlashAttributeReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8 == 0x02,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == GetFlashAttribute,
    flash_id: FuLenovoLdcFlashId,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcGetFlashAttributeRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 0x00,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == GetFlashAttribute,
    flash_id: FuLenovoLdcFlashId,
    _reserved: u8,
    flash_id_again: u8 == 0xFF,
    purpose: FuLenovoLdcExternalFlashIdPurpose,
    storage_size: u32le,
    erase_size: u16le,
    program_size: u16le,
}

#[repr(u8)]
enum FuLenovoLdcFlashMemorySelfVerifyType {
    Signature,
    Crc,
}

#[repr(u8)]
enum FuLenovoLdcFlashMemorySelfVerifyResult {
	Fail,
    Pass,
}

#[repr(u8)]
enum FuLenovoLdcDockFwCtrlUpgradeStatus {
    NonLock,
    Locked,
}

#[repr(u8)]
enum FuLenovoLdcDockFwCtrlUpgradePhaseCtrl {
    Na,
    InPhase1,
    Unplug,
    NonUnplug,
    WaitForTimer,
}

#[derive(Default, New)]
struct FuStructLenovoLdcDfuControlReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8 == 0x02,
    cmd_class: FuLenovoLdcClassId == Dock,
	cmd_id: FuLenovoLdcExternalDockCmd == SetDockFirmwareUpgradeCtrl,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
    status: FuLenovoLdcDockFwCtrlUpgradeStatus,
    ctrl: FuLenovoLdcDockFwCtrlUpgradePhaseCtrl,
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcDfuControlRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 0x00,
    cmd_class: FuLenovoLdcClassId == Dock,
	cmd_id: FuLenovoLdcExternalDockCmd == SetDockFirmwareUpgradeCtrl,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoLdcGetFlashIdListReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8 == 0x00,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == GetFlashIdList,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcGetFlashIdListRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 0x01,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == GetFlashIdList,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
    total: u8,
}

#[derive(Default, New)]
struct FuStructLenovoLdcDockReadWithAddressReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8 == 0x08,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == GetFlashMemoryAccess,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
    memory_access_cmd: FuLenovoLdcFlashMemoryAccessCmd == DockReadWithAddress,
    _unknown: u8,
    size: u16le,
    addr: u32le,
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcDockReadWithAddressRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 262,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == GetFlashMemoryAccess,
    flash_id: FuLenovoLdcFlashId == None,
    _reserved: u8,
    _size: u16le,
    _addr: u32le,
    data: [u8; 256],
}

#[derive(Default, New)]
struct FuStructLenovoLdcDockEraseWithAddressReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8 == 0x08,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoLdcFlashId,
    _reserved: u8,
    memory_access_cmd: FuLenovoLdcFlashMemoryAccessCmd == DockEraseWithAddress,
    _unknown: u8,
    size: u16le,
    addr: u32le,
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcDockEraseWithAddressRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 0x0,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoLdcFlashId,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoLdcDockProgramWithAddressReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoLdcFlashId,
    _reserved: u8,
    memory_access_cmd: FuLenovoLdcFlashMemoryAccessCmd == DockProgramWithAddress,
    _unknown: u8,
    size: u16le,
    addr: u32le,
    // data: [u8; ProgramSize]
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcDockProgramWithAddressRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 0x0,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == SetFlashMemoryAccess,
    flash_id: FuLenovoLdcFlashId,
    _reserved: u8,
}

#[derive(Default, New)]
struct FuStructLenovoLdcFlashMemorySelfVerifyReq {
    target_status: FuLenovoLdcTargetStatus == CommandDefault,
    bufsz: u8 = 0x01,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == GetFlashMemorySelfVerify,
    flash_id: FuLenovoLdcFlashId,
    _reserved: u8,
    type: FuLenovoLdcFlashMemorySelfVerifyType == Crc,
}

#[derive(Default, Parse)]
struct FuStructLenovoLdcFlashMemorySelfVerifyRes {
    target_status: FuLenovoLdcTargetStatus == CommandSuccess,
    bufsz: u8 == 0x4,
    cmd_class: FuLenovoLdcClassId == ExternalFlash,
	cmd_id: FuLenovoLdcExternalFlashCmd == GetFlashMemorySelfVerify,
    flash_id: FuLenovoLdcFlashId,
    _reserved: u8,
    crc: u32le,
}