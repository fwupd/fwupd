/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CORSAIR_SUBDEVICE (fu_corsair_subdevice_get_type())
G_DECLARE_FINAL_TYPE(FuCorsairSubdevice, fu_corsair_subdevice, FU, CORSAIR_SUBDEVICE, FuDevice)

struct _FuCorsairSubdeviceClass {
	FuDeviceClass parent_class;
};

FuCorsairSubdevice *
fu_corsair_subdevice_new(FuDevice *proxy);
