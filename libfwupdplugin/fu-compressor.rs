// Copyright 2026 Red Hat
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString, FromString)]
#[repr(u8)]
enum FuCompressorFormat {
    Raw,
    Zlib,
    Gzip,
}
