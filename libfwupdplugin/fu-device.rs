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
    UseCache = 1 << 5,
}

// Flags to use when incorporating a device instance.
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
    InstanceIds = 1 << 17,
    // Set the possible plugins.
    PossiblePlugins = 1 << 18,
    // Set the device GType.
    Gtype = 1 << 19,
    // Set the device instance keys.
    InstanceKeys = 1 << 20,
    // Set the proxy device.
    Proxy = 1 << 21,
    // Set the private flags.
    PrivateFlags = 1 << 22,
    // Sets the created and modified values.
    CreatedModified = 1 << 23,
    // Sets the device equivalent ID.
    EquivalentId = 1 << 24,
    // Call the set_quirk_kv() vfunc for the superclassed object.
    SetQuirkKv = 1 << 25,
    // Sets the metadata.
    Metadata = 1 << 26,
    // Sets the various parent IDs.
    ParentIds = 1 << 27,
    // Sets the required free space.
    RequiredFree = 1 << 28,
    // Sets the update request ID.
    UpdateRequestId = 1 << 29,
    // Sets the custom flags.
    CustomFlags = 1 << 30,
    // Sets the event target.
    Target = 1 << 31,
    // All flags
    All = u64::MAX,
}
