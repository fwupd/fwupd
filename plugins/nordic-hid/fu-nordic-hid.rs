// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructNordicHidDfuFwinfo {
    flash_area_id: u8,
    flashed_image_len: u32le,
    ver_major: u8,
    ver_minor: u8,
    ver_rev: u16le,
    ver_build_nr: u32le,
}
