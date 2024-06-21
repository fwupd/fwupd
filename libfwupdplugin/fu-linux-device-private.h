/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-linux-device.h"

FuLinuxDevice *
fu_linux_device_new(FuContext *ctx, const gchar *sysfs_path);
void
fu_linux_device_set_io_channel(FuLinuxDevice *self, FuIOChannel *io_channel) G_GNUC_NON_NULL(1, 2);
