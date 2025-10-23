/*
 * Copyright 2025 hya1711 <591770796@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(u8)]
enum FuLegionHidUpgradeStep {
        Start = 0x50,
        QuerySize,
        WriteData,
        Verify,
}

#[repr(u8)]
enum FuLegionHidResponseStatus {
        Ok = 0x00,
        Fail,
        Busy,
}

#[derive(New, Getters, Setters, Default, ParseStream)]
#[repr(C, packed)]
struct FuStructLegionHidUpgradeCmd {
        report_id: u8,
        length: u8,
        main_id: u8 = 0x53,
        sub_id: u8 = 0x11,
        device_id: u8,
        param: u8,
        data: [u8; 58],
}

#[derive(New, Getters, Setters, Default, ParseStream)]
#[repr(C, packed)]
struct FuStructLegionHidUpgradeStartParam {
        length: u8 = 0x08,
        step: FuLegionHidUpgradeStep = Start,
        flag: u8 = 0x00,
        crc16: u16be,
        size_high: u8,
        size_mid: u8,
        size_low: u8,
        sn: u8 = 0x01,
}

#[derive(New, Getters, Setters, Default, ParseStream)]
#[repr(C, packed)]
struct FuStructLegionHidNormalCmd {
        report_id: u8,
        length: u8,
        main_id: u8,
        sub_id: u8,
        device_id: u8,
        data: [u8; 59],
}

#[derive(New, Getters, Setters, Default, ParseStream)]
#[repr(C, packed)]
struct FuStructLegionHidBinHeader {
        mcu_size: u32le,
        mcu_version: u32le,
        left_size: u32le,
        left_version: u32le,
        right_size: u32le,
        right_version: u32le,
}
