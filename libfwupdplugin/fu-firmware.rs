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

enum FuFirmwareAlignment {
    1,
    2,
    4,
    8,
    16,
    32,
    64,
    128,
    256,
    512,
    1K,
    2K,
    4K,
    8K,
    16K,
    32K,
    64K,
    128K,
    256K,
    512K,
    1M,
    2M,
    4M,
    8M,
    16M,
    32M,
    64M,
    128M,
    256M,
    512M,
    1G,
    2G,
    4G,
}
