// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
#[repr(u16be)]
enum FuJabraGnpPacketCmd {
    GetName           = 0x0200,
    GetVersion        = 0x0203,
    GetDfuPid         = 0x0213,
    GetFwuProtocol    = 0x0214,
    GetBatteryLevel   = 0x1202,
    Start             = 0x0F17,
    FlashEraseDone    = 0x0F18,
    WriteCrc          = 0x0F19,
    WriteChunk        = 0x0F1A,
    WriteChunks       = 0x0F1B,
    ReadVerifyStatus  = 0x0F1C,
    WriteDfuFromSquif = 0x0F1D,
    WriteVersion      = 0x0F1E,
    WritePartition    = 0x0F2D,
}

#[derive(New, Default, Parse)]
#[repr(C, packed)]
struct FuStructJabraGnpPacket {
    iface: u8 == 0x05,
    dst: u8,
    src: u8,
    sequence_number: u8,
    cmd_length: u8 = 0x46,
    cmd: FuJabraGnpPacketCmd,
    //payload: [u8; 57],
}
