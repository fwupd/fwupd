// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuCh347CmdSpi {
    SetCfg = 0xC0,
    CsCtrl = 0xC1,
    OutIn  = 0xC2,
    In     = 0xC3,
    Out    = 0xC4,
    GetCfg = 0xCA,
}

#[derive(New, Getters)]
struct FuStructCh347Req {
    cmd: FuCh347CmdSpi,
    payloadsz: u16le,
}
