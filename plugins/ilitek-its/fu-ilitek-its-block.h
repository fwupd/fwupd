/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ILITEK_ITS_BLOCK (fu_ilitek_its_block_get_type())
G_DECLARE_FINAL_TYPE(FuIlitekItsBlock, fu_ilitek_its_block, FU, ILITEK_ITS_BLOCK, FuFirmware)

FuFirmware *
fu_ilitek_its_block_new(void);
guint16
fu_ilitek_its_block_get_crc(FuIlitekItsBlock *self) G_GNUC_NON_NULL(1);
