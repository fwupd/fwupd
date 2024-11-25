// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum FuIntelThunderboltNvmSection {
    Digital,
    Drom,
    ArcParams,
    DramUcode,
}

#[derive(ToString, FromString)]
enum FuIntelThunderboltNvmFamily {
    Unknown,
    FalconRidge,
    WinRidge,
    AlpineRidge,
    AlpineRidgeC,
    TitanRidge,
    Bb,
    MapleRidge,
    GoshenRidge,
    BarlowRidge,
}
