// Copyright (C) 2023 Canonical Ltd
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ParseStream, New)]
struct FuStructSbatLevelSectionHeader {
    version: u32 == 0x0,
    previous: u32,
    latest: u32,
}
