// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuCfiDeviceCmd {
    ReadId,
    PageProg,
    ChipErase,
    ReadData,
    ReadStatus,
    SectorErase,
    WriteEn,
    WriteStatus,
    BlockErase,
}
