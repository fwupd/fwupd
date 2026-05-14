// Copyright 2026 Sean Rhodes <sean@starlabs.systems>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuSunplusCameraXuUnit {
    Id = 0x04,
}

#[repr(u8)]
enum FuSunplusCameraSelector {
    Reg16Cmd = 0x02,
    Reg8Data = 0x03,
    Enable = 0x09,
    Access = 0x0A,
    Checksum = 0x0B,
    Finish = 0x0C,
    ReadAddr = 0x0E,
    ReadChunk = 0x16,
}

#[derive(ToString)]
enum FuSunplusCameraAsicRegister {
    DownloadStateReg = 0x2501,
}
