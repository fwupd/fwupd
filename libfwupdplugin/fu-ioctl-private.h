/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-ioctl.h"
#include "fu-udev-device.h"

FuIoctl *
fu_ioctl_new(FuUdevDevice *udev_device, const gchar *name) G_GNUC_NON_NULL(1);
