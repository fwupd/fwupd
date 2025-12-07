// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The kind of remote.
// Since: 0.9.6
#[derive(ToString, FromString)]
enum FwupdRemoteKind {
    // Unknown kind.
    Unknown,
    // Requires files to be downloaded.
    Download,
    // Reads files from the local machine.
    Local,
    // Reads directory from the local machine.
    // Since: 1.2.4
    Directory,
}

// The flags available for the remote.
// Since: 1.9.4
#[derive(ToString(enum), FromString(enum))]
enum FwupdRemoteFlags {
    // No flags set.
    None = 0,
    // Is enabled.
    Enabled = 1 << 0,
    // Requires approval for each firmware.
    ApprovalRequired = 1 << 1,
    // Send firmware reports automatically.
    AutomaticReports = 1 << 2,
    // Send security reports automatically.
    AutomaticSecurityReports = 1 << 3,
    // Use peer-to-peer locations for metadata.
    // Since: 1.9.5
    AllowP2pMetadata = 1 << 4,
    // Use peer-to-peer locations for firmware.
    // Since: 1.9.5
    AllowP2pFirmware = 1 << 5,
    // Do not slow deployment using phased updates.
    // Since: 2.0.17
    NoPhasedUpdates = 1 << 6,
}
