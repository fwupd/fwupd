// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuDeviceInstanceFlags {
    None    = 0,
    Visible = 1 << 0,
    Quirks  = 1 << 1,
    Generic = 1 << 2, // added by a baseclass
    Counterpart = 1 << 3,
    Deprecated = 1 << 4,
}

// Flags to use when incorporating a device instance.
// Since: 2.0.0
enum FuDeviceIncorporateFlags {
    // Set baseclass properties.
    Baseclass = 1 << 0,
    // Set superclass properties, implemented using `->incorporate()`.
    Superclass = 1 << 1,
    // Set vendor.
    Vendor = 1 << 2,
    // Set vendor IDs.
    VendorIds = 1 << 3,
    // Set the physical ID.
    PhysicalId = 1 << 4,
    // Set the logical ID.
    LogicalId = 1 << 5,
    // Set the backend ID.
    BackendId = 1 << 6,
    // Set the remove delay.
    RemoveDelay = 1 << 7,
    // Set the acquiesce delay.
    AcquiesceDelay = 1 << 8,
    // Set the icons.
    Icons = 1 << 9,
    // Set the update error.
    UpdateError = 1 << 10,
    // Set the update state.
    UpdateState = 1 << 11,
    // Set the vendor ID.
    Vid = 1 << 12,
    // Set the product ID.
    Pid = 1 << 13,
    // Set the update message.
    UpdateMessage = 1 << 14,
    // Set the update image.
    UpdateImage = 1 << 15,
    // Add the device events.
    Events = 1 << 16,
    // Set the instance IDs.
    // Since: 2.0.4
    InstanceIds = 1 << 17,
    // Set the possible plugins.
    // Since: 2.0.6
    PossiblePlugins = 1 << 18,
    // Set the device GType.
    // Since: 2.0.6
    Gtype = 1 << 19,
    // Set the device instance keys.
    // Since: 2.0.9
    InstanceKeys = 1 << 20,
    // All flags
    All = u64::MAX,
}
