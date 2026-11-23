// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuTcgOpalFlags {
    Unknown = 0,
    Supported = 1 << 0,
    LockingSupported = 1 << 1,
    LockingEnabled = 1 << 2,
    Locked = 1 << 3,
    MbrEnabled = 1 << 4,
    MbrDone = 1 << 5,
    SumSupported = 1 << 6,
}
