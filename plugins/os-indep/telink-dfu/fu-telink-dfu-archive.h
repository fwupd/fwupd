/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TELINK_DFU_ARCHIVE (fu_telink_dfu_archive_get_type())
G_DECLARE_FINAL_TYPE(FuTelinkDfuArchive, fu_telink_dfu_archive, FU, TELINK_DFU_ARCHIVE, FuFirmware)
