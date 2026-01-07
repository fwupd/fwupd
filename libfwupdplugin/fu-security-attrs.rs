// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Flags to use when calculating an HSI version.
enum FuSecurityAttrsFlags {
    // No flags set
    None = 0,
    // Add the daemon version to the HSI string
    AddVersion = 1 << 0,
}
