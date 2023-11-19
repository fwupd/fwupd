// Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
// SPDX-License-Identifier: LGPL-2.1+

#[repr(u8)]
enum QcReportId {
    Command = 3,
    DataTransfer = 5,
    Response = 6,
}

#[derive(New)]
struct QcHidCommand {
    report_id: QcReportId == Command,
    payload_len: u8,
    payload: [u8; 61],
}

#[derive(Parse)]
struct QcHidResponse {
    report_id: QcReportId == Response,
    payload_len: u8,
    payload: [u8; 11],
}

#[derive(New)]
struct QcHidDataTransfer {
    report_id: QcReportId == DataTransfer,
    payload_len: u8,
    payload: [u8; 253],
}
