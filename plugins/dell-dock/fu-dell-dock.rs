// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuDellDockMstCmd {
    EnableRemoteControl = 0x1,
    DisableRemoteControl = 0x2,
    Checksum = 0x11,
    EraseFlash = 0x14,
    Crc16Checksum = 0x17, // Cayenne specific
    ActivateFw = 0x18, // Cayenne specific
    WriteFlash = 0x20,
    WriteMemory = 0x21,
    ReadFlash = 0x30,
    ReadMemory = 0x31,
}
