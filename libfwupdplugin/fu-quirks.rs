// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Flags to use when loading quirks.
enum FuQuirksLoadFlags {
    // No flags set
    None = 0,
    // Ignore readonly filesystem errors
    ReadonlyFs = 1 << 0,
    // Do not save to a persistent cache
    NoCache = 1 << 1,
    // Do not check the key files for errors
    NoVerify = 1 << 2,
}
