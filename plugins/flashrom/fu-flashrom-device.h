/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_FLASHROM_DEVICE (fu_flashrom_device_get_type ())
G_DECLARE_FINAL_TYPE (FuFlashromDevice, fu_flashrom_device, FU, FLASHROM_DEVICE, FuDevice)

FuDevice	*fu_flashrom_device_new			(void);
