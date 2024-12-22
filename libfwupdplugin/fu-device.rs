// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToBitString)]
enum FuDeviceInstanceFlag {
    None    = 0,
    Visible = 1 << 0,
    Quirks  = 1 << 1,
    Generic = 1 << 2, // added by a baseclass
    Counterpart = 1 << 3,
}
