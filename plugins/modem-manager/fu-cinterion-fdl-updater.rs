/*
 * Copyright 2024 TDT AG <development@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(u8)]
enum FuCinterionFdlResponse {
    Ok = 0x01,
    Retry = 0x02,
    Unknown = 0x03,
    Busy = 0x04,
}
