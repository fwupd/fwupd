// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum IntelMeMcaSection {
    Me = 0x00,
    Uep = 0x04,
    Fpf = 0x08,
}
