// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructEp963xFirmwareHdr {
    reserved: [u8; 16],
    magic: [char; 5] == "EP963",
}

// byte = 0x07
#[derive(ToString)]
enum FuEp963xSmbusError {
    None = 0x00,
    Address = 0x01,
    NoAck = 0x02,
    Arbitration = 0x04,
    Command = 0x08,
    Timeout = 0x10,
    Busy = 0x20,
}

enum FuEp963Icp {
    Enter = 0x40,
    Exit = 0x82,
    Bank = 0x83,
    Address = 0x84,
    Readblock = 0x85,
    Writeblock = 0x86,
    Mcuid = 0x87,
    Done = 0x5A,
}

enum FuEp963Opcode {
    SmbusRead = 0x01,
    EraseSpi = 0x02,
    ResetBlockIndex = 0x03,
    WriteBlockData = 0x04,
    ProgramSpiBlock = 0x05,
    ProgramSpiFinish = 0x06,
    GetSpiChecksum = 0x07,
    ProgramEpFlash = 0x08,
    GetEpChecksum = 0x09,
    StartThrowPage = 0x0b,
    GetEpSiteType = 0x0c,
    CommandVersion = 0x10,
    CommandStatus = 0x20,
    SubmcuEnterIcp = 0x30,
    SubmcuResetBlockIdx = 0x31,
    SubmcuWriteBlockData = 0x32,
    SubmcuProgramBlock = 0x33,
    SubmcuProgramFinished = 0x34,
}

enum FuEp963UfCmd {
    Version = 0x00,
    Enterisp = 0x01,
    Program = 0x02,
    Read = 0x03,
    Mode = 0x04,
}

/* byte 0x02 */
enum FuEp963UsbState {
    Ready = 0x00,
    Busy = 0x01,
    Fail = 0x02,
    Unknown = 0xff,
}
