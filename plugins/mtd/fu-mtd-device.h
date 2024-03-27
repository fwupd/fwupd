/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_MTD_DEVICE (fu_mtd_device_get_type())
G_DECLARE_FINAL_TYPE(FuMtdDevice, fu_mtd_device, FU, MTD_DEVICE, FuUdevDevice)
