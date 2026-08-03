// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuCliOperation {
    Unknown,
    Update,
    Downgrade,
    Install,
    Read,
}

enum FuCliCmdFlags {
    None = 0,
    IsAlias = 1 << 0,
}

#[derive(ToString)]
enum FuCliDependencyKind {
    Unknown,
    Runtime,
    Compile,
}

enum FuSecurityAttrToStringFlags {
    None = 0,
    ShowObsoletes = 1 << 0,
    ShowUrls = 1 << 1,
}
