// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructLenovoLdcHdr {
    signature: u8 == 0xDE,
    address: u16le,
}

#[derive(ToString)]
enum FuLenovoLdcStatus {
    Unknown,
    Failed,
}

enum FuLenovoLdcTargetStatus {
	CommandDefault = 0x00,
	CommandBusy = 0x01,
	CommandSuccess = 0x02,
	CommandFaliure = 0x03,
	CommandTimeout = 0x04,
	CommandNotSupport = 0x05,
}

enum FuLenovoLdcClassId {
	DeviceInformation = 0x00,
	Dfu = 0x09,
	Test = 0x0A,
	ExternalFlash = 0x0C,
	Dock = 0x0E,
}

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

enum FuLenovoLdcSignType {
    Unsigned,
    Rsa2048,
    Rsa3072,
    Ecc256,
    Ecc384,
}

enum FuLenovoLdcExternalFlashIdPurpose {
    Common,
    ApplicationData,
    ImageData,
    FirmwareFile,
}

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

enum FuLenovoLdcFlashMemoryAccessCtrl {
    Release,
    Request,
}

enum FuLenovoLdcFlashMemorySelfVerifyType {
    Signature,
    Crc,
}

enum FuLenovoLdcFlashMemorySelfVerifyResult {
	Fail,
    Pass,
}

enum FuLenovoLdcDockFwCtrlUpgradeStatus {
    NonLock,
    Locked,
}

enum FuLenovoLdcDockFwCtrlUpgradePhaseCtrl {
    Na,
    InPhase1,
    Unplug,
    NonUnplug,
    WaitForTimer,
}