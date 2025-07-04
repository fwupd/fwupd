/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mtd-device.h"

#define FU_TYPE_MTD_IFD_DEVICE (fu_mtd_ifd_device_get_type())
G_DECLARE_FINAL_TYPE(FuMtdIfdDevice, fu_mtd_ifd_device, FU, MTD_IFD_DEVICE, FuDevice)

FuMtdIfdDevice *
fu_mtd_ifd_device_new(FuDevice *parent, FuIfdImage *img);
