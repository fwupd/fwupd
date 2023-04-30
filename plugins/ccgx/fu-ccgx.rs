// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString, FromString)]
enum CcgxImageType {
    Unknown,
    Single,
    DualSymmetric,          // A/B runtime
    DualAsymmetric,         // A=bootloader (fixed) B=runtime
    DualAsymmetricVariable, // A=bootloader (variable) B=runtime
    DmcComposite,           // composite firmware image for dmc
}
#[derive(ToString)]
enum CcgxFwMode {
    Boot,
    Fw1,
    Fw2,
    Last,
}
