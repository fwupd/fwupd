// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuFirmwareExportFlags {
    None = 0,
    IncludeDebug = 1 << 0,
    AsciiData = 1 << 1, // as UTF-8 strings
}
