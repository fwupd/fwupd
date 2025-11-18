// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Bitfield, ToString)]
enum FuCpuDeviceFlag {
    None    = 0,
    Shstk   = 1 << 0,
    Ibt     = 1 << 1,
    Tme     = 1 << 2,
    Smap    = 1 << 3,
}
