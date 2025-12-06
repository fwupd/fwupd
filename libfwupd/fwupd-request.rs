// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The kind of request we are asking of the user.
// Since: 1.6.2
#[derive(ToString, FromString)]
enum FwupdRequestKind {
    // Unknown kind.
    Unknown,
    // After the update.
    Post,
    // Immediately.
    Immediate,
}

// Flags used to represent request attributes
// Since: 1.8.6
#[derive(ToString(enum), FromString(enum))]
enum FwupdRequestFlags {
    // No flags are set.
    None = 0,
    // Use a generic (translated) request message.
    AllowGenericMessage = 1 << 0,
    // Use a generic (translated) request image.
    AllowGenericImage = 1 << 1,
    // Device requires a non-generic interaction with custom non-translatable text.
    // Since: 1.9.10
    NonGenericMessage = 1 << 2,
    // Device requires to show the user a custom image for the action to make sense.
    // Since: 1.9.10
    NonGenericImage = 1 << 3,
    // The request flag is unknown, typically caused by using mismatched client and daemon.
    Unknown = u64::MAX,
}
