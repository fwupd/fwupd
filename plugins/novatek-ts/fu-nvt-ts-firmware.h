/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

#include "fu-nvt-ts-plugin.h"

#define FU_TYPE_NVT_TS_FIRMWARE (fu_nvt_ts_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuNvtTsFirmware, fu_nvt_ts_firmware, FU, NVT_TS_FIRMWARE, FuFirmware)

FuFirmware *
fu_nvt_ts_firmware_new(void);

void
fu_nvt_ts_firmware_bin_clear(FuNvtTsFwBin *fwb);

gboolean
fu_nvt_ts_firmware_prepare_fwb(FuNvtTsFirmware *self,
			       FuNvtTsFwBin *fwb,
			       guint32 flash_start_addr,
			       guint32 flash_max_size,
			       GError **error);
