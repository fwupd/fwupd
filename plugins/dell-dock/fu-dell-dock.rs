/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[repr(u8)]
enum DockBaseType {
    Unknown = 0x0,
    Salomon = 0x4,
    Atomic = 0x5,
    K2 = 0x10,
}
