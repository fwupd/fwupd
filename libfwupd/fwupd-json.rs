// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// JSON node kind.
// Since: 2.1.1
#[derive(ToString)]
enum FwupdJsonNodeKind {
    Raw,
    String,
    Array,
    Object,
}

// JSON export flags.
// Since: 2.1.1
enum FwupdJsonExportFlags {
    None = 0,
    Indent = 1 << 0,
}

// JSON load flags.
// Since: 2.1.1
enum FwupdJsonLoadFlags {
    None = 0,
    Trusted = 1 << 0,
    StaticKeys = 1 << 1,
}
