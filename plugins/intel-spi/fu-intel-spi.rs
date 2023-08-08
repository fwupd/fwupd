// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString, FromString)]
enum IntelSpiKind {
    Unknown,
    Apl,
    C620,
    Ich0,
    Ich2345,
    Ich6,
    Ich9,
    Pch100,
    Pch200,
    Pch300,
    Pch400,
    Poulsbo,
}
