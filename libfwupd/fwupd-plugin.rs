// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Flags used to represent plugin attributes
// Since: 1.5.0
#[derive(ToString(enum), FromString(enum))]
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
