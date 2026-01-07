// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuConfigLoadFlags {
    None = 0,
    WatchFiles = 1 << 0,
    FixPermissions = 1 << 1,
    MigrateFiles = 1 << 2,
}
