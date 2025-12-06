// Copyright 2025 Harris Tai <harris_tai@pixart.com>
// Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

// ---- TP enums exported to C via rustgen ----

#[repr(u8)]
enum FuPxiTpResetMode {
    Application,
    Bootloader,
}

#[repr(u8)]
enum FuPxiTpResetKey1 {
    Suspend = 0xaa,
}

#[repr(u8)]
enum FuPxiTpResetKey2 {
    Regular    = 0xbb,
    Bootloader = 0xcc,
}

#[repr(u8)]
enum FuPxiTpFlashInst {
    Cmd0 = 0x00,
    Cmd1 = 0x01,
}

#[repr(u8)]
enum FuPxiTpFlashExecState {
    Busy    = 0x01,
    Success = 0x00,
}

#[repr(u8)]
enum FuPxiTpFlashWriteEnable {
    Success = 0x02,
}

#[repr(u8)]
enum FuPxiTpFlashStatus {
    Busy = 0x01,
}

#[repr(u32)]
enum FuPxiTpFlashCcr {
    WriteEnable = 0x00000106,
    ReadStatus  = 0x01000105,
    EraseSector = 0x00002520,
    ProgramPage = 0x01002502,
}

#[repr(u16)]
enum FuPxiTpPartId {
    Pjp274 = 0x0274,
}

#[repr(u8)]
enum FuPxiTpCrcCtrl {
    FwBank0    = 0x02,
    FwBank1    = 0x10,
    ParamBank0 = 0x04,
    ParamBank1 = 0x20,
    Busy       = 0x01,
}


// ---- TF enums exported to C via rustgen ----

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
