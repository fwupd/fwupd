// Copyright 2023 Canonical Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ParseStream, New, Default)]
struct FuStructSbatLevelSectionHeader {
    version: u32le == 0x0,
    previous: u32le,
    latest: u32le,
}
