// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(FromString)]
enum FuDaemonMachineKind {
    Unknown,
    Physical,
    Virtual,
    Container,
}
