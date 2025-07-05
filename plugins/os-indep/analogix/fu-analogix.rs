// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuAnalogixUpdateStatus {
    Invalid,
    Start,
    Finish,
    Error = 0xFF,
}
