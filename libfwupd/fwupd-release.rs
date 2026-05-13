// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Flags used to represent release attributes
// Since: 1.2.6
#[derive(ToString(enum), FromString(enum))]
enum FwupdReleaseFlags {
    // No flags are set.
    None = 0,
    // The payload binary is trusted.
    TrustedPayload = 1 << 0,
    // The payload metadata is trusted.
    TrustedMetadata = 1 << 1,
    // The release is newer than the device version.
    IsUpgrade = 1 << 2,
    // The release is older than the device version.
    IsDowngrade = 1 << 3,
    // The installation of the release is blocked as below device version-lowest.
    BlockedVersion = 1 << 4,
    // The installation of the release is blocked as release not approved by an administrator.
    BlockedApproval = 1 << 5,
    // The release is an alternate branch of firmware.
    // Since: 1.5.0
    IsAlternateBranch = 1 << 6,
    // The release is supported by the community and not the hardware vendor.
    // Since: 1.7.5
    IsCommunity = 1 << 7,
    // The payload has been tested by a report we trust.
    // Since: 1.9.1
    TrustedReport = 1 << 8,
    // The release flag is unknown, typically caused by using mismatched client and daemon.
    Unknown = u64::MAX,
}

// The release urgency.
// Since: 1.4.0
#[derive(ToString, FromString)]
enum FwupdReleaseUrgency {
    // Unknown.
    Unknown,
    // Low.
    Low,
    // Medium.
    Medium,
    // High.
    High,
    // Critical, e.g. a security fix.
    Critical,
}
