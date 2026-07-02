// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Bitfield, ToString)]
enum FuMtdIntelSpiStatus {
    None = 0,
    Found = 1 << 0,
    Supported = 1 << 1,
    Protected = 1 << 2,
}
