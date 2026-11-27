// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuProgressFlags {
    None            = 0,
    Guessed         = 1 << 0,
    NoProfile       = 1 << 1,
    ChildFinished   = 1 << 2,
    NoTraceback     = 1 << 3,
    NoSender        = 1 << 4,
    Unknown         = u64::MAX,
}
