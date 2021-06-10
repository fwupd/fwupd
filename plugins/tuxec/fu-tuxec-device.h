/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

struct _FuTuxecDeviceClass {
	FuUdevDeviceClass		 parent_class;
};

#define FU_TYPE_TUXEC_DEVICE (fu_tuxec_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuTuxecDevice, fu_tuxec_device, FU,
			  TUXEC_DEVICE, FuUdevDevice)

void		 fu_tuxec_device_set_programmer_name	(FuTuxecDevice *self,
							 const gchar	*name);
const gchar	*fu_tuxec_device_get_programmer_name (FuTuxecDevice *self);
void		 fu_tuxec_device_set_programmer_args	(FuTuxecDevice *self,
							 const gchar	*args);
gsize		 fu_tuxec_device_get_flash_size	(FuTuxecDevice *self);
struct flashrom_flashctx *fu_tuxec_device_get_flashctx (FuTuxecDevice *self);
