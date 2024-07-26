/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#[repr(u8)] // EC USB HID host command
enum DellDock2EcHidCmd {
    SetDockPkg = 0x01,
    GetDockInfo = 0x02,
    GetDockData = 0x03,
    GetDockType = 0x05,
    SetModifyLock = 0x0a,
    SetFwupMode = 0x0b,
    SetPassive = 0x0d,
}

#[repr(u8)] // FuDellDock2DockInfoStructure::FuDellDock2EcAddrMap::location
enum DellDock2EcLocation {
    Base = 0x00,
    Module,
}

#[repr(u8)] // FuDellDock2DockInfoStructure::FuDellDock2EcAddrMap::device_type
enum DellDock2EcDevType{
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

#[repr(u8)] // subtype to DellDock2EcDevType::Usbhub
enum DellDock2EcDevUsbhubSubtype {
    Rts5480 = 0,
    Rts5485,
}

#[repr(u8)] // subtype to DellDock2EcDevType::Tbt
enum DellDock2EcDevTbtSubtype {
    Tr = 0,
    Gr,
    Br,
}

#[repr(u8)] // subtype to DellDock2EcDevType::Mst
enum DellDock2EcDevMstSubtype {
    Vmm8430 = 0,
    Vmm9430,
}

#[repr(u8)] // subtype to DellDock2EcDevType::Pd
enum DellDock2EcDevPdSubtype {
    Ti = 0,
}

#[repr(u8)] // instance to EcDockDevicePdSubtype::Ti
enum DellDock2EcDevPdSubtypeTiInstance {
    Up5 = 0,
    Up15,
    Up17,
}

#[repr(u8)] // FuDellDock2DockDataStructure::module_type
enum DellDock2EcModuleType {
    NoModule = 0x00,
    Watt130Dp = 0x04,
    Watt130Universal = 0x05,
    Watt210DualC = 0x07,
    Watt130Tbt4 = 0x08,
    QiCharger = 0xa0,
    WifiRmm = 0xa1,
    Unknown = 0xfe,
}

#[repr(u8)] // EcHidCmd::SetPassive
enum DellDock2EcPassiveAction {
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
