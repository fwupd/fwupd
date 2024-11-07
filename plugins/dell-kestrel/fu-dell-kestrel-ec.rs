/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#[repr(u8)]
enum DellDockBaseType {
    Unknown = 0x0,
    Kestrel = 0x07,
}

#[repr(u8)]
enum DellKestrelEcHidCmd {
    SetDockPkg = 0x01,
    GetDockInfo = 0x02,
    GetDockData = 0x03,
    GetDockType = 0x05,
    SetModifyLock = 0x0a,
    SetFwupMode = 0x0b,
    SetPassive = 0x0d,
}

#[repr(u8)]
enum DellKestrelEcLocation {
    Base = 0x00,
    Module,
}

#[repr(u8)]
enum DellKestrelEcDevType{
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
enum DellKestrelEcDevSubtype {
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
enum DellKestrelEcDevInstance {
    TiUp5 = 0,
    TiUp15,
    TiUp17,
}

#[repr(u8)]
enum DellKestrelDockSku {
    Dpalt = 0x01,
    T4,
    T5,
}

#[repr(u8)]
enum DellKestrelEcRespToChunk {
    UpdateComplete = 1,
    SendNextChunk,
    UpdateFailed,
}

#[repr(C, packed)]
struct DellKestrelPackageFwVersions {
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
