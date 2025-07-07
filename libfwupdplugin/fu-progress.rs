// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuProgressFlags {
    None            = 0,        // Since: 1.7.0
    Guessed         = 1 << 0,   // Since: 1.7.0
    NoProfile       = 1 << 1,   // Since: 1.7.0
    ChildFinished   = 1 << 2,   // Since: 1.8.2
    NoTraceback     = 1 << 3,   // Since: 1.8.2
    NoSender        = 1 << 4,   // Since: 1.9.10
    Unknown         = u64::MAX, // Since: 1.7.0
}
