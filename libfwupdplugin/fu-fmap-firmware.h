/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_FMAP_FIRMWARE (fu_fmap_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuFmapFirmware, fu_fmap_firmware, FU, FMAP_FIRMWARE, FuFirmware)

FuFirmware			*fu_fmap_firmware_new			(void);
