// Copyright 2025 Harris Tai <harris_tai@pixart.com>
// Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

// TF write-simple command header:
//   REPORT_ID__PASS_THROUGH
//   TF_FRAME_PREAMBLE
//   SLAVE_ADDRESS
//   TF_FUNC_WRITE_SIMPLE
//   addr (LE16)
//   len  (LE16)
#[derive(New)]
#[repr(C, packed)]
struct FuStructPxiTfWriteSimpleCmd {
    report_id: u8,
    preamble: u8,
    slave_addr: u8,
    func: u8,
    addr: u16le,
    len: u16le,
}

// TF write-with-packet command header:
//   REPORT_ID__PASS_THROUGH
//   TF_FRAME_PREAMBLE
//   SLAVE_ADDRESS
//   TF_FUNC_WRITE_WITH_PACK
//   addr         (LE16)
//   datalen      (LE16)  // payload bytes: 2 bytes total + 2 bytes index + data
//   packet_total (LE16)
//   packet_index (LE16)
#[derive(New)]
#[repr(C, packed)]
struct FuStructPxiTfWritePacketCmd {
    report_id: u8,
    preamble: u8,
    slave_addr: u8,
    func: u8,
    addr: u16le,
    datalen: u16le,
    packet_total: u16le,
    packet_index: u16le,
}

// TF read-with-length command header:
//   REPORT_ID__PASS_THROUGH
//   TF_FRAME_PREAMBLE
//   SLAVE_ADDRESS
//   TF_FUNC_READ_WITH_LEN
//   addr      (LE16)
//   datalen   (LE16)  // in_buf length + 2 bytes reply length
//   reply_len (LE16)  // expected reply payload length (hint)
#[derive(New)]
#[repr(C, packed)]
struct FuStructPxiTfReadCmd {
    report_id: u8,
    preamble: u8,
    slave_addr: u8,
    func: u8,
    addr: u16le,
    datalen: u16le,
    reply_len: u16le,
}
