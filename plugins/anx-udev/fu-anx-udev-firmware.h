/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include "fu-firmware.h"

#include "fu-anx-udev-common.h"

#define FU_TYPE_ANX_UDEV_FIRMWARE (fu_anx_udev_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuAnxUdevFirmware, fu_anx_udev_firmware, FU,\
                        ANX_UDEV_FIRMWARE, FuFirmware)
FuFirmware    *fu_anx_udev_firmware_new (void);