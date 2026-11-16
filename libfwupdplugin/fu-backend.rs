// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Flags used to represent backend attributes
// Since: 2.2.1
#[derive(ToString, Bitfield)]
enum FuBackendFlags {
    // No backend flags are set.
    None = 0,
    // The backend is enabled.
    Enabled = 1 << 0,
    // The backend is enabled.
    CanInvalidate = 1 << 1,
    // The backend been setup.
    DoneSetup = 1 << 2,
}
