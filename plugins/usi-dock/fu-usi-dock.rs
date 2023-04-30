// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum UsiDockSpiState {
    None,
    SwitchSuccess,
    SwitchFail,
    CmdSuccess,
    CmdFail,
    RwSuccess,
    RwFail,
    Ready,
    Busy,
    Timeout,
    FlashFound,
    FlashNotFound,
}
