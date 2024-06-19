/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(u8)] // EC USB HID host command
enum EcV2HidCmd {
    SetDockPkg = 0x01,
    GetDockInfo = 0x02,
    GetDockData = 0x03,
    GetDockType = 0x05,
    SetModifyLock = 0x0a,
    SetFwupMode = 0x0b,
    SetDockReset = 0x0c,
    SetPassive = 0x0d,
    GetDockCompInfo = 0x10,
    GetDockPortInfo = 0x11,
    GetUpdateRdyStatus = 0x1d,
}

#[repr(u8)] // EcV2HidCmd::SetModifyLock
enum EcV2ModifyLockDevice {
    Ec = 0x01,
    UsbHubPrimary = 0x07,
    UsbHubSecondary = 0x08,
    MstHub = 0x09,
    Tbt = 0x0a,
}

#[repr(u8)] // EcV2HidCmd::GetUpdateRdyStatus
enum EcV2DockUpdate {
    Available = 0x00,
    Unavailable,
}

#[repr(u8)] // FuDellDockVer2DockInfoStructure::FuDellDockV2EcAddrMap::location
enum EcV2Location {
    Base = 0x00,
    Module,
}

#[repr(u8)] // FuDellDockVer2DockInfoStructure::FuDellDockV2EcAddrMap::device_type
enum EcV2DockDeviceType{
    MainEc = 0x00,
    Pd,
    Usbhub,
    Mst,
    Tbt,
    Qi,
    DpMux,
    Lan,
    Fan,
    Rmm,
    Wtpd,
}

#[repr(u8)] // subtype to EcV2DockDeviceType::Usbhub
enum EcV2DockDeviceUsbhubSubtype {
    Rts5480 = 0,
    Rts5485,
}

#[repr(u8)] // subtype to EcV2DockDeviceType::Tbt
enum EcV2DockDeviceTbtSubtype {
    Tr = 0,
    Gr,
    Br,
}

#[repr(u8)] // subtype to EcV2DockDeviceType::Mst
enum EcV2DockDeviceMstSubtype {
    Vmm8430 = 0,
    Vmm9430,
}

#[repr(u8)] // subtype to EcV2DockDeviceType::Pd
enum EcV2DockDevicePdSubtype {
    Ti = 0,
}

#[repr(u8)] // instance to EcV2DockDevicePdSubtype::Ti
enum EcV2DockDevicePdSubtypeTiInstance {
    Up5 = 0,
    Up15,
    Up17,
}

#[repr(u8)] // FuDellDockVer2DockDataStructure::module_type
enum EcV2ModuleType {
    NoModule = 0x00,
    Watt130Dp = 0x04,
    Watt130Universal = 0x05,
    Watt210DualC = 0x07,
    Watt130Tbt4 = 0x08,
    QiCharger = 0xa0,
    WifiRmm = 0xa1,
    Unknown = 0xfe,
}

#[repr(u8)] // FuDellDockVer2DockDataStructure::dock_status
enum EcV2PortStatus {
    DataModeTbt = 0,
    DataModeDp,
    DataModeUsb3,
    DataModeDellDocking,
    DataModeDellExtPower,
    DataModeUsb4,
    DataModeUsb2,
    PowerRoleSink,
    PowerRoleSource,
    Usb4PcieTunneling,
    VproSupport,
}

#[repr(u8)] // EcV2HidCmd::SetPassive
enum EcV2PassiveAction {
    FlashEc = 1,
    RebootDock = 2,
    AuthTbt = 4,
}

#[repr(u8)] // private enum for dock sku
enum K2DockSku {
    Dpalt = 0x01,
    Tbt4,
    Tbt5,
}
