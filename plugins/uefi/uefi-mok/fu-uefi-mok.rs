// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(FromString)]
enum FuUefiMokHsiKey {
    None                            = 0,
    ShimHasNxCompatSet              = 1 << 0,
    HeapIsExecutable                = 1 << 1,
    StackIsExecutable               = 1 << 2,
    RoSectionsAreWritable           = 1 << 3,
    HasMemoryAttributeProtocol      = 1 << 4,
    HasDxeServicesTable             = 1 << 5,
    HasGetMemorySpaceDescriptor     = 1 << 6,
    HasSetMemorySpaceAttributes     = 1 << 7,
}
