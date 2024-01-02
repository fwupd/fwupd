// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ValidateStream, ParseStream)]
struct DfuCsrFile {
    file_id: [char; 8] == "CSR-dfu2",
    file_version: u16le == 0x02,
    file_len: u32le,
    file_hdr_len: u16le,
    //file_desc: [char; 64], -- useless
}

#[repr(u8)]
enum DfuCsrReportId {
    Command = 0x01,
    Status = 0x02,
    Control = 0x03,
}

#[repr(u8)]
enum DfuCsrCommand {
    Upgrade = 0x01,
}

#[derive(New)]
struct DfuCsrCommandHeader {
    report_id: DfuCsrReportId,
    command: DfuCsrCommand,
    idx: u16le,
    chunk_sz: u16le,
}
