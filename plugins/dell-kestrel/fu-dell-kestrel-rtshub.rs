/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#[repr(C, packed)]
#[derive(New, Setters, Getters)]
struct FuStructRtshubHidCmdBuf {
    cmd: u8,
    ext: u8,
    regaddr: u32le,
    bufferlen: u16le,
    reserved: [u8; 56],
    data: [u8; 128],
}
