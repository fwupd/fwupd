// Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
#[repr(u8)]
enum FuQcReportId {
    Command = 3,
    DataTransfer = 5,
    Response = 6,
}

#[derive(New)]
struct FuStructQcHidCommand {
    report_id: FuQcReportId == Command,
    payload_len: u8,
    payload: [u8; 61],
}

#[derive(Parse)]
struct FuStructQcHidResponse {
    report_id: FuQcReportId == Response,
    payload_len: u8,
    payload: [u8; 11],
}

#[derive(New)]
struct FuStructQcHidDataTransfer {
    report_id: FuQcReportId == DataTransfer,
    payload_len: u8,
    payload: [u8; 253],
}
