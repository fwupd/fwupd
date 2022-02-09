/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_IFD_DEVICE (fu_ifd_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIfdDevice, fu_ifd_device, FU, IFD_DEVICE, FuDevice)

struct _FuIfdDeviceClass {
	FuDeviceClass parent_class;
};

FuDevice *
fu_ifd_device_new(FuContext *ctx, FuIfdRegion region, guint32 freg);
void
fu_ifd_device_set_access(FuIfdDevice *self, FuIfdRegion region, FuIfdAccess access);
