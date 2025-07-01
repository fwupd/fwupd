// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuFirmwareExportFlags {
    None = 0,
    IncludeDebug = 1 << 0,
    AsciiData = 1 << 1, // as UTF-8 strings
}

enum FuFirmwareParseFlags {
    None = 0,
    IgnoreChecksum = 1 << 6,
    IgnoreVidPid = 1 << 7,
    NoSearch = 1 << 8, // no heuristics
    CacheStream = 1 << 10,
    CacheBlob = 1 << 11,
}
