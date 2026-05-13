// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Flags used when calling fu_ioctl_execute() and fu_udev_device_ioctl().
enum FuIoctlFlags {
    // No flags set
    None = 0,
    // Retry the call on failure
    Retry = 1 << 0,
    // The @ptr passed to ioctl is an integer, not a buffer
    PtrAsInteger = 1 << 1,
}
