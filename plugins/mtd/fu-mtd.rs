// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Bitfield, ToString)]
enum FuMtdIntelSpiFlags {
    None = 0,
    Protected = 1 << 0,
    BiosLocked = 1 << 1,
    SmmBwp = 1 << 2,
}
