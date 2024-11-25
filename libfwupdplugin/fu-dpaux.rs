// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(Parse)]
struct FuStructDpauxDpcd {
    ieee_oui: u24be,
    dev_id: [char; 6],
    hw_rev: u8,
    fw_ver: u24be, // technically this is u16be, but both MST vendors do this
}
