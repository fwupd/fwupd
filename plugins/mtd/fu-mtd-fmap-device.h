/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-mtd-device.h"

#define FU_TYPE_MTD_FMAP_DEVICE (fu_mtd_fmap_device_get_type())
G_DECLARE_FINAL_TYPE(FuMtdFmapDevice, fu_mtd_fmap_device, FU, MTD_FMAP_DEVICE, FuDevice)

FuMtdFmapDevice *
fu_mtd_fmap_device_new(FuDevice *parent, FuFirmware *img);
