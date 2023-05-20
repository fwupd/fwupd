// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Getters)]
struct FpcDfu {
    status: u8,
    max_payload_size: u8,
    _reserved: [u8; 2],
    state: u8,
    _reserved2: u8,
}
