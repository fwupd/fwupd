// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Bitfield, FromString(enum))]
enum FuBackendFlags {
    None                    = 0,
    SortDevices             = 1 << 0,
}
