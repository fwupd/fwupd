// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The flags to use when converting data from one form to another.
// Since: 2.0.0
enum FwupdCodecFlags {
    // No flags set.
    None = 0,
    // Include values that may be regarded as trusted or sensitive.
    Trusted = 1 << 0,
    // Compress values to the smallest possible size.
    // Since: 2.0.8
    Compressed = 1 << 1,
}
