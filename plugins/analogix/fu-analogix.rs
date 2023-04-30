// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum AnalogixUpdateStatus {
    Invalid,
    Start,
    Finish,
    Error = 0xFF,
}
