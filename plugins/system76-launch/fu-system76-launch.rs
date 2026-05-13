// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuSystem76LaunchSecurityState {
    Lock,           // flashing is prevented, cannot be set with CMD_SECURITY_SET
    Unlock,         // flashing is allowed, cannot be set with CMD_SECURITY_SET
    PrepareLock,    // flashing will be prevented on the next reboot
    PrepareUnlock,  // flashing will be allowed on the next reboot
}
