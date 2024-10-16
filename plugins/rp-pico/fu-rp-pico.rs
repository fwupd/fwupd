// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// control requests:
enum FuRpPicoResetRequest {
    Bootsel             = 0x01, // to UF2
    Reset               = 0x02, // to application
}
