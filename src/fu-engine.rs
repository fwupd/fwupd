// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(FromString)]
enum ReleasePriority {
    None,
    Local,
    Remote,
}

#[derive(FromString)]
enum P2pPolicy {
    Nothing = 0x00,
    Metadata = 0x01,
    Firmware = 0x02,
}
