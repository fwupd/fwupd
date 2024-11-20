/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#[repr(u8)]
enum FuDellDockBaseType {
    Unknown = 0x0,
    Kestrel = 0x07,
}

#[repr(u8)]
enum FuDellKestrelEcHidCmd {
    SetDockPkg = 0x01,
    GetDockInfo = 0x02,
    GetDockData = 0x03,
    GetDockType = 0x05,
    SetModifyLock = 0x0a,
    SetFwupMode = 0x0b,
    SetPassive = 0x0d,
}

#[repr(u8)]
enum FuDellKestrelEcLocation {
    Base = 0x00,
    Module,
}

#[repr(u8)]
enum FuDellKestrelEcDevType{
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

#[repr(u8)]
enum FuDellKestrelEcDevSubtype {
    Rts0 = 0x00,
    Rts5 = 0x01,
    Tr = 0x00,
    Gr = 0x01,
    Br = 0x02,
    Vmm8 = 0x00,
    Vmm9 = 0x01,
    Ti = 0x00,
}

#[repr(u8)]
enum FuDellKestrelEcDevInstance {
    TiUp5 = 0,
    TiUp15,
    TiUp17,
}

#[repr(u8)]
enum FuDellKestrelDockSku {
    Dpalt = 0x01,
    T4,
    T5,
}

#[repr(u8)]
enum FuDellKestrelEcRespToChunk {
    UpdateComplete = 1,
    SendNextChunk,
    UpdateFailed,
}

#[repr(C, packed)]
#[derive(New, Getters, Parse)]
struct FuStructDellKestrelDockData {
    dock_configuration: u8,
    dock_type: u8,
    reserved: u16le,
    module_type: u16le,
    reserved: u16le,
    reserved: u16le,
    reserved: u16le,
    dock_firmware_pkg_ver: u32le,
    module_serial: u64le,
    reserved: u64le,
    service_tag: [char; 7],
    marketing_name: [char; 32],
    reserved: u32le,
    reserved: u32le,
    reserved: u32le,
    reserved: u8,
    dock_status: u32le,
    reserved: u16le,
    reserved: u16le,
    dock_mac_addr: [u8; 6],
    reserved: u32le,
    reserved: u32le,
    reserved: u32le,
    reserved: u32le,
    reserved: u16le,
    eppid: u8,
    reserved: [u8; 74],
}

#[repr(C, packed)]
struct FuStructDellKestrelPackageFwVersions {
    pkg_ver: u32le,
    ec_ver: u32le,
    mst_ver: u32le,
    rts0_g2_ver: u32le,
    rts5_g2_ver: u32le,
    rts0_g1_ver: u32le,
    tbt_ver: u32le,
    pd_up5_ver: u32le,
    pd_up15_ver: u32le,
    pd_up17_ver: u32le,
    dpmux_ver: u32le,
    rmm_ver: u32le,
    lan_ver: u32le,
    reserved: [u32le; 3],
}

/* dock info  */
#[repr(C, packed)]
#[derive(Getters)]
struct FuStructDellKestrelDockInfoEcAddrMap {
    location: u8,
    device_type: u8,
    subtype: u8,
    arg: u8,
    instance: u8,
}

#[repr(C, packed)]
#[derive(Getters)]
struct FuStructDellKestrelDockInfoEcQueryEntry {
    ec_addr_map: FuStructDellKestrelDockInfoEcAddrMap,
    version_32: u32be,
}

#[repr(C, packed)]
struct FuStructDellKestrelDockInfoHeader {
    total_devices: u8,
    first_index: u8,
    last_index: u8,
}

#[repr(C, packed)]
#[derive(New, Getters, Parse)]
struct FuStructDellKestrelDockInfo {
    header: FuStructDellKestrelDockInfoHeader,
    devices: [FuStructDellKestrelDockInfoEcQueryEntry; 20],
}
