// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum SynapticsMstMode {
    Unknown,
    Direct,
    Remote,
}
#[derive(ToString)]
enum SynapticsMstFamily {
    Unknown,
    Tesla,
    Leaf,
    Panamera,
    Cayenne,
    Spyder,
}
