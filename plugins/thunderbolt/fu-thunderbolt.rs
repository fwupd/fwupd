// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString, FromString)]
enum FuThunderboltControllerKind {
    Unknown,
    Host,
    Device,
    Hub,
}
