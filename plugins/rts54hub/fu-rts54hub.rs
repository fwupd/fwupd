// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuRts54hubRtd21xxIspStatus {
    Busy = 0xbb,        // host must wait for device
    IdleSuccess = 0x11, // previous command was OK
    IdleFailure = 0x12, // previous command failed
}
