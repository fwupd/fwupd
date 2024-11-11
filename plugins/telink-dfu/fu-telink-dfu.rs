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
struct FuStructTelinkDfuEndCheck {
    pkt_index: u16,
    inverted_pkt_index: u16,
}

#[derive(New, Getters, Default)]
struct FuStructTelinkDfuBlePkt {
    preamble: u16,
    payload: [u8; 16] = 0xFF,
    crc: u16,
}

#[derive(New, Getters, Default)]
struct FuStructTelinkDfuHidPktPayload {
    ota_cmd: u16 = 0xFFFF,
    ota_data: [u8; 16] = 0xFF,
    crc: u16 = 0xFFFF,
}

#[derive(New, Getters, Default)]
struct FuStructTelinkDfuHidPkt {
    op_code: u8 = 0x02,
    ota_data_len: u16 = 0x0000,
    payload: FuStructTelinkDfuHidPktPayload,
}

#[derive(New, Getters, Default)]
struct FuStructTelinkDfuHidLongPkt {
    op_code: u8 = 0x02,
    ota_data_len: u16 = 0x0000,
    payload_1: FuStructTelinkDfuHidPktPayload,
    payload_2: FuStructTelinkDfuHidPktPayload,
    payload_3: FuStructTelinkDfuHidPktPayload,
}
