/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCAL_MOC_FIRMWARE (fu_focal_moc_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuFocalMocFirmware, fu_focal_moc_firmware, FU, FOCAL_MOC_FIRMWARE, FuFirmware)

FuFirmware *
fu_focal_moc_firmware_new(void);
