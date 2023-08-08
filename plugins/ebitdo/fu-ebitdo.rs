// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Parse)]
struct EbitdoHdr {
    version: u32le,
    destination_addr: u32le,
    destination_len: u32le,
    reserved: [u32le; 4],
}
#[repr(u8)]
enum EbitdoPktType {
    UserCmd = 0x00,
    UserData = 0x01,
    MidCmd = 0x02,
}
#[derive(ToString)]
#[repr(u8)]
enum EbitdoPktCmd {
    FwUpdateData       = 0x00, // update firmware data
    FwUpdateHeader     = 0x01, // update firmware header
    FwUpdateOk         = 0x02, // mark update as successful
    FwUpdateError      = 0x03, // update firmware error
    FwGetVersion       = 0x04, // get cur firmware vision
    FwSetVersion       = 0x05, // set firmware version
    FwSetEncodeId      = 0x06, // set app firmware encode ID
    Ack                = 0x14, // acknowledge
    Nak                = 0x15, // negative acknowledge
    UpdateFirmwareData = 0x16, // update firmware data
    TransferAbort      = 0x18, // aborts transfer
    VerificationId     = 0x19, // verification id (only BT?)
    GetVerificationId  = 0x1a, // verification id (only BT)
    VerifyError        = 0x1b, // verification error
    VerifyOk           = 0x1c, // verification successful
    TransferTimeout    = 0x1d, // send or receive data timeout
    GetVersion         = 0x21, // get fw ver joystick mode
    GetVersionResponse = 0x22, // get fw version response
}
#[derive(New, Parse)]
struct EbitdoPkt {
    pkt_len: u8,
    type: EbitdoPktType,
    subtype: u8,
    cmd_len: u16le,
    cmd: EbitdoPktCmd,
    payload_len: u16le, // optional
}
