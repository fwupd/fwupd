// Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuTelinkDfuCmd {
    OtaFwVersion    = 0xff00,
    OtaStart        = 0xff01,
    OtaEnd          = 0xff02,
    OtaStartReq     = 0xff03,
    OtaStartRsp     = 0xff04,
    OtaTest         = 0xff05,
    OtaTestRsp      = 0xff06,
    OtaError        = 0xff07,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructTelinkDfuEndCheck {
    pkt_index: u16le,
    inverted_pkt_index: u16le,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructTelinkDfuBlePkt {
    preamble: u16le,
    payload: [u8; 16] = [0xFF; 16],
    crc: u16le,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructTelinkDfuHidPktPayload {
    ota_cmd: u16le = 0xFFFF,
    ota_data: [u8; 16] = [0xFF; 16],
    crc: u16le = 0xFFFF,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructTelinkDfuHidPkt {
    op_code: u8 = 0x02,
    ota_data_len: u16le,
    payload: FuStructTelinkDfuHidPktPayload,
}

#[derive(New, Getters, Default)]
#[repr(C, packed)]
struct FuStructTelinkDfuHidLongPkt {
    op_code: u8 = 0x02,
    ota_data_len: u16le,
    payload_1: FuStructTelinkDfuHidPktPayload,
    payload_2: FuStructTelinkDfuHidPktPayload,
    payload_3: FuStructTelinkDfuHidPktPayload,
}
