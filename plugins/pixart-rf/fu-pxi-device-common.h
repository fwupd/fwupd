/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "config.h"


#include "fu-plugin.h"
#include "fu-pxi-device.h"
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <linux/input.h>



gboolean
fu_pxi_device_set_feature (FuDevice *self,
                           const guint8 *data,
                           guint datasz,
                           GError **error);


gboolean
fu_pxi_device_get_hid_raw_info(FuDevice *self,
                               struct hidraw_devinfo *info,
                               GError **error);


gboolean
fu_pxi_device_get_feature (FuDevice *self,
                           guint8 *data,
                           guint datasz,
                           GError **error);

void
fu_pxi_device_calculate_checksum(gushort* checksum, gsize sz, const guint8* data);

