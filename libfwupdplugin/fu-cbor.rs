// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuCborTag {
    PosInt,
    NegInt,
    Bytes,
    String,
    Array,
    Map,
    Semantic,
    Special,
}

enum FuCborLen {
    ShortMax  = 23,
    Ext8  = 24,
    Ext16 = 25,
    Ext32 = 26,
    Ext64 = 27,
    Indefinite = 31,
}

#[derive(ToString)]
enum FuCborSpecialValue {
    False = 20,
    True = 21,
    Null = 22,
    Undefined = 23,
    Extended = 24,
    Float16 = 25,
    Float32 = 26,
    Float64 = 27,
    Break = 31,
}

#[derive(ToString)]
enum FuCborItemKind {
    Integer,
    Bytes,
    String,
    Array,
    Map,
    Boolean,
}
