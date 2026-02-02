/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

#include "fu-novatek-ts-plugin.h"

#define FU_TYPE_NOVATEK_TS_FIRMWARE (fu_novatek_ts_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuNovatekTsFirmware,
		     fu_novatek_ts_firmware,
		     FU,
		     NOVATEK_TS_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_novatek_ts_firmware_new(void);

gboolean
fu_novatek_ts_firmware_prepare_bin(FuNovatekTsFirmware *self,
				   guint8 **bin_data,
				   guint32 *bin_size,
				   guint32 flash_start_addr,
				   guint32 flash_max_size,
				   GError **error);
