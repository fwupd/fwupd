// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToBitString)]
enum FuIoChannelOpenFlag {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Nonblock = 1 << 2,
    Sync = 1 << 3,
}
