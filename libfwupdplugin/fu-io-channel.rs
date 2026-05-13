// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuIoChannelOpenFlags {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Nonblock = 1 << 2,
    Sync = 1 << 3,
}

// Flags used when reading data from the TTY.
// Since: 1.2.2
enum FuIoChannelFlags {
    // No flags are set.
    None = 0,
    // Only one read or write is expected.
    SingleShot = 1 << 0,
    // Flush pending input before writing.
    FlushInput = 1 << 1,
    // Block waiting for the TTY.
    UseBlockingIo = 1 << 2,
}
