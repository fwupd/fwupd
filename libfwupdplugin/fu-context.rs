// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Bitfield)]
enum FuContextFlags {
    None                    = 0,
    SaveEvents              = 1 << 0, // so that they can be replayed to emulate devices
    SystemInhibit           = 1 << 1, // all devices are not updatable
    LoadedHwinfo            = 1 << 2, // has called fu_context_load_hwinfo()
    InhibitVolumeMount      = 1 << 3, // usually for self tests
    FdeBitlocker            = 1 << 4, // full disk encryption
    FdeSnapd                = 1 << 5, // full disk encryption
    IgnoreEfivarsFreeSpace  = 1 << 6, // ignore the free space requirement for db, dbx, etc
    NoIdleSources           = 1 << 7,
    InsecureUefi            = 1 << 8,
    IsHypervisor            = 1 << 9,
    IsHypervisorPrivileged  = 1 << 10, // privileged xen can access most hardware
    IsContainer             = 1 << 11,
    SmbiosUefiEnabled       = 1 << 12,
}

enum FuContextHwidFlags {
    None                    = 0,
    LoadConfig              = 1 << 0,
    LoadSmbios              = 1 << 1,
    LoadFdt                 = 1 << 2,
    LoadDmi                 = 1 << 3,
    LoadKenv                = 1 << 4,
    LoadDarwin              = 1 << 5,
    WatchFiles              = 1 << 6,
    FixPermissions          = 1 << 7,
}

enum FuContextEspFileFlags {
    None                    = 0,
    IncludeFirstStage       = 1 << 0, // e.g. shim
    IncludeSecondStage      = 1 << 1, // e.g. grub
    IncludeRevocations      = 1 << 2, // e.g. the `revocations.efi` file used by shim
}

enum FuContextQuirkSource {
    Device,     // perhaps a USB descriptor
    File,       // an internal .quirk file.
    Db,         // populated from usb.ids and pci.ids
    Fallback,   // perhaps from the PCI class information
}
