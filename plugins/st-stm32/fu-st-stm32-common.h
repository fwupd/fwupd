/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-st-stm32-struct.h"

#define FU_ST_STM32_CRC_INIT_VALUE 0xFFFFFFFF

gboolean
fu_st_stm32_crc(guint32 *crc_inout, guint8 *buf, gsize len, GError **error);
FuStStm32Cmd
fu_st_stm32_cmd_base(FuStStm32Cmd cmd);
