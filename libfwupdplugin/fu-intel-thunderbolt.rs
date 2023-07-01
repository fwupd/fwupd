// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum IntelThunderboltNvmSection {
    Digital,
    Drom,
    ArcParams,
    DramUcode,
    Last,
}

#[derive(ToString, FromString)]
enum IntelThunderboltNvmFamily {
    Unknown,
    FalconRidge,
    WinRidge,
    AlpineRidge,
    AlpineRidgeC,
    TitanRidge,
    Bb,
    MapleRidge,
    GoshenRidge,
}
