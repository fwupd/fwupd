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

#[derive(ToString)]
enum FuFirmwareFlags {
    None = 0,
    DedupeId = 1 << 0,
    DedupeIdx = 1 << 1,
    HasChecksum = 1 << 2, // or CRC
    HasVidPid = 1 << 3,
    DoneParse = 1 << 4,
    HasStoredSize = 1 << 5,
    AlwaysSearch = 1 << 6, // useful has an *unparsed* variable-length header
    NoAutoDetection = 1 << 7, // has no known header
    HasCheckCompatible = 1 << 8,
    IsLastImage = 1 << 9, // use for FuLinearFirmware when padding is present
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
