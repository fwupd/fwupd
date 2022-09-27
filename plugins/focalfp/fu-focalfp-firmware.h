/*
 * Copyright (C) 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCALFP_FIRMWARE (fu_focalfp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuFocalfpFirmware, fu_focalfp_firmware, FU, FOCALFP_FIRMWARE, FuFirmware)

FuFirmware *
fu_focalfp_firmware_new(void);
guint32
fu_focalfp_firmware_get_checksum(FuFocalfpFirmware *self);
