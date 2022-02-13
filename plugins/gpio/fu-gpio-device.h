/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GPIO_DEVICE (fu_gpio_device_get_type())
G_DECLARE_FINAL_TYPE(FuGpioDevice, fu_gpio_device, FU, GPIO_DEVICE, FuUdevDevice)

gboolean
fu_gpio_device_assign(FuGpioDevice *self, const gchar *id, gboolean value, GError **error);
gboolean
fu_gpio_device_unassign(FuGpioDevice *self, GError **error);
