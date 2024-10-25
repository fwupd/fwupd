/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#[repr(u8)] // EC USB HID host command
enum DellK2EcHidCmd {
    SetDockPkg = 0x01,
    GetDockInfo = 0x02,
    GetDockData = 0x03,
    GetDockType = 0x05,
    SetModifyLock = 0x0a,
    SetFwupMode = 0x0b,
    SetPassive = 0x0d,
}

#[repr(u8)] // FuDellK2DockInfoStructure::FuDellK2EcAddrMap::location
enum DellK2EcLocation {
    Base = 0x00,
    Module,
}

#[repr(u8)] // FuDellK2DockInfoStructure::FuDellK2EcAddrMap::device_type
enum DellK2EcDevType{
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

#[repr(u8)] // subtype to DellK2EcDevType::Usbhub
enum DellK2EcDevUsbhubSubtype {
    Rts5480 = 0,
    Rts5485,
}

#[repr(u8)] // subtype to DellK2EcDevType::Tbt
enum DellK2EcDevTbtSubtype {
    Tr = 0,
    Gr,
    Br,
}

#[repr(u8)] // subtype to DellK2EcDevType::Mst
enum DellK2EcDevMstSubtype {
    Vmm8430 = 0,
    Vmm9430,
}

#[repr(u8)] // subtype to DellK2EcDevType::Pd
enum DellK2EcDevPdSubtype {
    Ti = 0,
}

#[repr(u8)] // instance to EcDockDevicePdSubtype::Ti
enum DellK2EcDevPdSubtypeTiInstance {
    Up5 = 0,
    Up15,
    Up17,
}

#[repr(u8)] // FuDellK2DockDataStructure::module_type
enum DellK2EcModuleType {
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
enum DellK2EcPassiveAction {
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

#[repr(u8)] // dock resp to chunk write
enum DellK2EcRespToChunk {
    UpdateComplete = 1,
    SendNextChunk,
    UpdateFailed,
}

#[repr(C, packed)]
#[derive(New, Getters, Parse)]
struct FuStructDellK2DockData {
    dock_configuration: u8,
    dock_type: u8,
    power_supply_wattage: u16,
    module_type: u16,
    board_id: u16,
    port0_dock_status: u16,
    port1_dock_status: u16,
    dock_firmware_pkg_ver: u32,
    module_serial: u64,
    original_module_serial: u64,
    service_tag: [char; 7],
    marketing_name: [char; 32],
    dock_error: u32,
    dock_module_status: u32,
    dock_module_error: u32,
    reserved: u8,
    dock_status: u32,
    dock_state: u16,
    dock_config: u16,
    dock_mac_addr: [u8; 6],
    dock_capabilities: u32,
    dock_policy: u32,
    dock_temperature: u32,
    dock_fan_speed: u32,
    upf_power: u16,
    eppid: u8,
    reserved: [u8; 74],
}
