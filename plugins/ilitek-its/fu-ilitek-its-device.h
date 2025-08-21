/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ILITEK_ITS_DEVICE (fu_ilitek_its_device_get_type())
G_DECLARE_FINAL_TYPE(FuIlitekItsDevice, fu_ilitek_its_device, FU, ILITEK_ITS_DEVICE, FuHidrawDevice)

gboolean
fu_ilitek_its_device_register_drm_device(FuIlitekItsDevice *self,
					 FuDrmDevice *drm_device,
					 GError **error) G_GNUC_NON_NULL(1, 2);
