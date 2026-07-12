// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuCliArgFlag {
    None = 0,
    AsJson = 1 << 0,
    DisableSslStrict = 1 << 1,
    AssumeYes = 1 << 2,
    ShowAll = 1 << 3,
    NoRemoteCheck = 1 << 4,
    NoMetadataCheck = 1 << 5,
    NoRebootCheck = 1 << 6,
    NoUnreportedCheck = 1 << 7,
    NoSafetyCheck = 1 << 8,
    NoDevicePrompt = 1 << 9,
    NoSecurityFix = 1 << 10,
    Sign = 1 << 11,
}
