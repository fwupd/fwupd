/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FLASHROM_DEVICE (fu_flashrom_device_get_type())
G_DECLARE_FINAL_TYPE(FuFlashromDevice, fu_flashrom_device, FU, FLASHROM_DEVICE, FuUdevDevice)

FuDevice *
fu_flashrom_device_new(FuContext *ctx);
