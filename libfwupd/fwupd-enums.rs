// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The flags to show daemon status.
// Since: 0.1.1
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

// Flags used to represent device attributes
// Since: 0.1.3
enum FwupdDeviceFlags {
    // No flags set
    None = 0ull,
    // Device is internal to the platform and cannot be removed easily.
    Internal = 1 << 0,
    // Device has the ability to be updated in this or any other mode.
    // Since: 0.9.7
    Updatable = 1 << 1,
    // Device requires an external power source to be connected or the battery
    // level at a minimum threshold to update.
    // Since: 0.6.3
    RequireAc = 1 << 3,
    // The device can not be updated without manual user interaction.
    // Since: 0.6.3
    Locked = 1 << 4,
    // The device is found in metadata loaded into the daemon.
    // Since: 0.7.1
    Supported = 1 << 5,
    // The device requires entering a bootloader mode to be manually.
    // Since: 0.7.3
    NeedsBootloader = 1 << 6,
    // The device requires a system reboot to apply firmware or to reload hardware.
    // Since: 0.9.7
    NeedsReboot = 1 << 8,
    // The success or failure of a previous update has been reported to a metadata server.
    // Since: 1.0.4
    Reported = 1 << 9,
    // The user has been notified about a change in the device state.
    // Since: 1.0.5
    Notified = 1 << 10,
    // The device is currently in a read-only bootloader mode and not running application code.
    // Since: 1.0.8
    IsBootloader = 1 << 13,
    // The device is in the middle of and update and the hardware is waiting to be probed or
    // replugged.
    // Since: 1.1.2
    WaitForReplug = 1 << 14,
    // The device requires the system to be shutdown to finish application of new firmware.
    // Since: 1.2.4
    NeedsShutdown = 1 << 17,
    // The device requires the update to be retried, possibly with a different plugin.
    // Since: 1.2.5
    AnotherWriteRequired = 1 << 18,
    // The device update needs to be separately activated.
    // This process may occur automatically on shutdown in some operating systems or when the
    // device is unplugged with some devices.
    // Since: 1.2.6
    NeedsActivation = 1 << 20,
    // The device is used for historical data only.
    // Since: 1.3.2
    Historical = 1 << 22,
    // The device will disappear after the update is complete and success or failure can't be
    // verified.
    // Since: 1.3.3
    WillDisappear = 1 << 24,
    // The device checksums can be compared against metadata.
    // Since: 1.3.3
    CanVerify = 1 << 25,
    // The device application firmware image can be dumped from device for verification.
    // Since: 1.3.3
    CanVerifyImage = 1 << 26,
    // The device firmware update architecture uses a redundancy mechanism such as A/B
    // partitions for updates.
    // Since: 1.3.3
    DualImage = 1 << 27,
    // In flashing mode, the device will only accept intended payloads and will revert back to
    // a valid firmware image if an invalid or incomplete payload was sent.
    // Since: 1.3.3
    SelfRecovery = 1 << 28,
    // The device remains usable while the update flashes or schedules the update.
    // The update will implicitly be applied next time the device is power cycled or possibly
    // activated.
    // Since: 1.3.3
    UsableDuringUpdate = 1 << 29,
    // All firmware updates for this device require a firmware version check.
    // Since: 1.3.7
    VersionCheckRequired = 1 << 30,
    // Install each intermediate releases for the device rather than jumping directly to the
    // newest.
    // Since: 1.3.7
    InstallAllReleases = 1 << 31,
    // The device is updatable but is currently inhibited from updates in the client.
    // Reasons include but are not limited to low power or requiring reboot from a previous
    // update.
    // Since: 1.4.1
    UpdatableHidden = 1 << 37,
    // The device supports switching to a different stream of firmware.
    // Since: 1.5.0
    HasMultipleBranches = 1 << 39,
    // The device firmware should be saved before installing firmware.
    // Since: 1.5.0
    BackupBeforeInstall = 1 << 40,
    // All devices with matching GUIDs will be updated at the same time.
    // For some devices it is not possible to have different versions of firmware
    // for hardware of the same type. Updating one device will force update of
    // others with exactly the same instance IDs.
    // Since: 1.6.2
    WildcardInstall = 1 << 42,
    // The device firmware can only be updated to a newer version and never downgraded or
    // reinstalled.
    // Since: 1.6.2
    OnlyVersionUpgrade = 1 << 43,
    // The device is currently unreachable, perhaps because it is in a lower power state or is
    // out of wireless range.
    // Since: 1.7.0
    Unreachable = 1 << 44,
    // The device is warning that a volume with full-disk-encryption was found on this machine,
    // typically a Windows NTFS partition with BitLocker.
    // Updating the firmware on this device may invalidate secrets used to decrypt the volume,
    // and the recovery key may be required.
    // Supported clients will display this information as a warning to the user.
    // Since: 1.7.1
    AffectsFde = 1 << 45,
    // The device is no longer supported by the original hardware vendor as it is considered
    // end-of-life. It it unlikely to receive firmware updates, even for security issues.
    // Since: 1.7.5
    EndOfLife = 1 << 46,
    // The firmware payload is verified on-device the payload using strong cryptography such
    // as RSA, AES or ECC.
    // It is usually not possible to modify or flash custom firmware not provided by the vendor.
    // Since: 1.7.6
    SignedPayload = 1 << 47,
    // The firmware payload is unsigned and it is possible to modify and flash custom firmware.
    // Since: 1.7.6
    UnsignedPayload = 1 << 48,
    // The device is emulated and should not be recorded by the backend.
    // Since: 1.8.11
    Emulated = 1 << 49,
    // The device should be recorded by the backend, allowing emulation.
    // Since: 1.8.11
    EmulationTag = 1 << 50,
    // The device should stay on one firmware version unless the new version is explicitly
    // specified.
    // This can either be done using `fwupdmgr install`, using GNOME Firmware, or using a BKC
    // config.
    // Since: 1.9.3
    OnlyExplicitUpdates = 1 << 51,
    // The device can be recorded by the backend, allowing emulation.
    // Since: 2.0.1
    CanEmulationTag = 1 << 52,
    // The device doesn't require verification of the newly installed version.
    // Since: 2.0.2
    InstallSkipVersionCheck = 1 << 53,
    // This flag is not defined, this typically will happen from mismatched fwupd library and
    // clients.
    // Since: 0.7.3
    Unknown = u64::MAX,
}

// Problems are reasons why the device is not updatable.
// All problems have to be fixable by the user, rather than the plugin author.
// Since: 1.8.1
enum FwupdDeviceProblem {
    // No device problems detected.
    None = 0,
    // The system power is too low to perform the update.
    SystemPowerTooLow = 1 << 0,
    // The device is unreachable, or out of wireless range.
    Unreachable = 1 << 1,
    // The device battery power is too low.
    PowerTooLow = 1 << 2,
    // The device is waiting for the update to be applied.
    UpdatePending = 1 << 3,
    // The device requires AC power to be connected.
    RequireAcPower = 1 << 4,
    // The device cannot be used while the laptop lid is closed.
    LidIsClosed = 1 << 5,
    // The device is emulated from a different host.
    // Since: 1.8.3
    IsEmulated = 1 << 6,
    // The device cannot be updated due to missing vendor's license.
    // Since: 1.8.6
    MissingLicense = 1 << 7,
    // The device cannot be updated due to a system-wide inhibit.
    // Since: 1.8.10
    SystemInhibit = 1 << 8,
    // The device cannot be updated as it is already being updated.
    // Since: 1.8.11
    UpdateInProgress = 1 << 9,
    // The device is in use and cannot be interrupted, for instance taking a phone call.
    // Since: 1.9.1
    InUse = 1 << 10,
    // The device cannot be used while there are no displays plugged in.
    // Since: 1.9.6
    DisplayRequired = 1 << 11,
    // We have two ways of communicating with one physical device, so we hide the worse one.
    // Since: 2.0.0
    LowerPriority = 1 << 12,
    // The device is signed with an insecure key
    // Since: 2.0.17
    InsecurePlatform = 1 << 13,
    // The firmware is locked in the system setup.
    // Since: 2.0.18
    FirmwareLocked = 1 << 14,
    // This problem is not defined, this typically will happen from mismatched
    // fwupd library and clients.
    Unknown = u64::MAX,
}

// Flags used to represent release attributes
// Since: 1.2.6
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

// Flags used to represent plugin attributes
// Since: 1.5.0
enum FwupdPluginFlags {
    // No plugin flags are set.
    None = 0,
    // The plugin has been disabled, either by daemon configuration or a problem.
    Disabled = 1 << 0,
    // The plugin has a problem and would like to show a user warning to a supported client.
    UserWarning = 1 << 1,
    // When the plugin loads it should clear the UPDATABLE flag from any devices.
    // This typically happens when the device requires a system restart.
    ClearUpdatable = 1 << 2,
    // The plugin won't load because no supported hardware was found.
    // This typically happens with plugins designed for a specific platform design
    // = such as the dell plugin only works on Dell systems,.
    NoHardware = 1 << 3,
    // The plugin discovered that UEFI UpdateCapsule are unsupported.
    // Supported clients will display this information to a user.
    CapsulesUnsupported = 1 << 4,
    // The plugin discovered that hardware unlock is required.
    // Supported clients will display this information to a user.
    UnlockRequired = 1 << 5,
    // The plugin discovered the efivar filesystem is not found and is required for this plugin.
    // Supported clients will display this information to a user.
    EfivarNotMounted = 1 << 6,
    // The plugins discovered that the EFI system partition was not found.
    // Supported clients will display this information to a user.
    EspNotFound = 1 << 7,
    // The plugin discovered the system is running in legacy CSM mode.
    // Supported clients will display this information to a user.
    LegacyBios = 1 << 8,
    // Failed to open plugin = missing dependency,.
    // Supported clients will display this information to a user.
    FailedOpen = 1 << 9,
    // A specific HWID is required to use this plugin.
    // Since: 1.5.8
    RequireHwid = 1 << 10,
    // The feature is not supported as the kernel is too old.
    // Since: 1.6.2
    KernelTooOld = 1 << 11,
    // The plugin requires the user to provide authentication details.
    // Supported clients will display this information to a user.
    // Since: 1.6.2
    AuthRequired = 1 << 12,
    // The plugin requires the config file to be saved with permissions that only allow the
    // root user to read.
    // Since: 1.8.5
    SecureConfig = 1 << 13,
    // The plugin is loaded from an external module.
    // Since: 1.8.6
    Modular = 1 << 14,
    // The plugin will be checked that it preserves system state such as `KEK`, `PK`,
    // `BOOT####` etc.
    // Since: 1.8.7
    MeasureSystemIntegrity = 1 << 15,
    // The plugins discovered that the EFI system partition may not be valid.
    // Supported clients will display this information to a user.
    // Since: 1.9.3
    EspNotValid = 1 << 16,
    // The plugin is ready for use and all devices have been coldplugged.
    // Since: 1.9.6
    Ready = 1 << 17,
    // The plugin is used for virtual devices that exercising daemon flows.
    // Since: 2.0.0
    TestOnly = 1 << 18,
    // Some devices supported by the plugin may cause a device to momentarily
    // stop working while probing.
    // Since: 2.0.12
    MutableEnumeration = 1 << 19,
    // The plugin flag is unknown.
    // This is usually caused by a mismatched libfwupdplugin and daemon.
    Unknown = u64::MAX,
}

// Flags to set when performing the firmware update or install.
// Since: 0.7.0
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
