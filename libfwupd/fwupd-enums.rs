// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The flags to show daemon status.
// Since: 0.1.1
#[derive(ToString, FromString)]
enum FwupdStatus {
    // Unknown state.
    Unknown,
    // Idle.
    Idle,
    // Loading a resource.
    Loading,
    // Decompressing firmware.
    Decompressing,
    // Restarting the device.
    DeviceRestart,
    // Writing to a device.
    DeviceWrite,
    // Verifying (reading) a device.
    DeviceVerify,
    // Scheduling an update for installation on reboot.
    Scheduling,
    // A file is downloading.
    // Since: 0.9.4
    Downloading,
    // Reading from a device.
    // Since: 1.0.0
    DeviceRead,
    // Erasing a device.
    // Since: 1.0.0
    DeviceErase,
    // Waiting for authentication.
    // Since: 1.0.0
    WaitingForAuth,
    // The device is busy.
    // Since: 1.0.1
    DeviceBusy,
    // The daemon is shutting down.
    // Since: 1.2.1
    Shutdown,
    // Waiting for an interactive user action.
    // Since: 1.9.8
    WaitingForUser,
}

// The flags to the feature capabilities of the front-end client.
// Since: 1.4.5
#[derive(ToString(enum), FromString(enum))]
enum FwupdFeatureFlags {
    // No trust.
    None = 0,
    // Can upload a report of the update back to the server.
    CanReport = 1 << 0,
    // Can perform detach action, typically showing text.
    DetachAction = 1 << 1,
    // Can perform update action, typically showing text.
    UpdateAction = 1 << 2,
    // Can switch the firmware branch.
    // Since: 1.5.0
    SwitchBranch = 1 << 3,
    // Can show interactive requests.
    // Since: 1.6.2
    Requests = 1 << 4,
    // Can warn about full disk encryption.
    // Since: 1.7.1
    FdeWarning = 1 << 5,
    // Can show information about community supported.
    // Since: 1.7.5
    CommunityText = 1 << 6,
    // Can show problems when getting the update list.
    // Since: 1.8.1
    ShowProblems = 1 << 7,
    // Can authenticate with PolicyKit for requests.
    // Since: 1.8.4
    AllowAuthentication = 1 << 8,
    // Can handle showing non-generic request message text.
    // Since: 1.9.8
    RequestsNonGeneric = 1 << 9,
    // Unknown flag.
    Unknown = u64::MAX,
}

// Flags to set when performing the firmware update or install.
// Since: 0.7.0
#[derive(ToString(enum;name=fwupd_install_flags_to_string;since=2.0.0), FromString(enum;name=fwupd_install_flags_from_string;since=2.0.4))]
enum FwupdInstallFlags {
    // No flags set.
    None = 0,
    // Allow reinstalling the same version.
    AllowReinstall = 1 << 1,
    // Allow downgrading firmware.
    AllowOlder = 1 << 2,
    // Force the update even if not a good idea.
    // Since: 0.7.1
    Force = 1 << 3,
    // Do not write to the history database.
    // Since: 1.0.8
    NoHistory = 1 << 4,
    // Allow firmware branch switching.
    // Since: 1.5.0
    AllowBranchSwitch = 1 << 5,
    // This is now unused; see #FuFirmwareParseFlags.
    // Since: 1.5.0
    IgnoreChecksum = 1 << 6,
    // This is now unused; see #FuFirmwareParseFlags.
    // Since: 1.5.0
    IgnoreVidPid = 1 << 7,
    // This is now only for internal use.
    // Since: 1.5.0
    NoSearch = 1 << 8,
    // Ignore version requirement checks.
    // Since: 1.9.21
    IgnoreRequirements = 1 << 9,
    // Only install to emulated devices.
    // Since: 2.0.10
    OnlyEmulated = 1 << 10,
    // Unknown flag
    Unknown = u64::MAX,
}

// FwupdSelfSignFlags:
// Flags to set when performing the firmware update or install.
// Since: 1.2.6
enum FwupdSelfSignFlags {
    // No flags set.
    None = 0,
    // Add the timestamp to the detached signature.
    AddTimestamp = 1 << 0,
    // Add the certificate to the detached signature.
    AddCert = 1 << 1,
    // Unknown flag
    Unknown = u64::MAX,
}

// The update state.
// Since: 0.7.0
#[derive(ToString, FromString)]
enum FwupdUpdateState {
    // Unknown.
    Unknown,
    // Update is pending.
    Pending,
    // Update was successful.
    Success,
    // Update failed.
    Failed,
    // Waiting for a reboot to apply.
    // Since: 1.0.4
    NeedsReboot,
    // Update failed due to transient issue, e.g. AC power required.
    // Since: 1.2.7
    FailedTransient,
}

// The flags used when parsing version numbers.
// If no verification is required then %PLAIN should be used to signify an unparsable text string.
// Since: 1.2.9
#[derive(ToString, FromString)]
enum FwupdVersionFormat {
    // Unknown version format.
    Unknown,
    // An unidentified format text string.
    Plain,
    // A single integer version number.
    Number,
    // Two AABB.CCDD version numbers.
    Pair,
    // Microsoft-style AA.BB.CCDD version numbers.
    Triplet,
    // UEFI-style AA.BB.CC.DD version numbers.
    Quad,
    // Binary coded decimal notation.
    Bcd,
    // Intel ME-style bitshifted notation.
    IntelMe,
    // Intel ME-style A.B.CC.DDDD notation notation, with offset 11.
    IntelMe2,
    // Legacy Microsoft Surface 10b.12b.10b.
    // Since: 1.3.4
    SurfaceLegacy,
    // Microsoft Surface 8b.16b.8b.
    // Since: 1.3.4
    Surface,
    // Dell BIOS BB.CC.DD style.
    // Since: 1.3.6
    DellBios,
    // Hexadecimal 0xAABCCDD style.
    // Since: 1.4.0
    Hex,
    // Dell BIOS AA.BB.CC style.
    // Since: 1.9.24
    DellBiosMsb,
    // Intel ME-style bitshifted notation, with offset 19.
    // Since: 2.0.4
    IntelCsme19,
}
