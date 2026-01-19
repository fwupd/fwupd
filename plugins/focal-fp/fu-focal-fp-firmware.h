/*
 * Copyright 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCAL_FP_FIRMWARE (fu_focal_fp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuFocalFpFirmware, fu_focal_fp_firmware, FU, FOCAL_FP_FIRMWARE, FuFirmware)

guint32
fu_focal_fp_firmware_get_checksum(FuFocalFpFirmware *self);
