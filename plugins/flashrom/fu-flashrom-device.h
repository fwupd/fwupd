/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

struct _FuFlashromDeviceClass {
	FuDeviceClass			 parent_class;
};

#define FU_TYPE_FLASHROM_DEVICE (fu_flashrom_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuFlashromDevice, fu_flashrom_device, FU,
			  FLASHROM_DEVICE, FuDevice)

void		 fu_flashrom_device_set_programmer_name	(FuFlashromDevice *self,
							 const gchar *name);
gchar		*fu_flashrom_device_get_programmer_name (FuFlashromDevice *self);
void		 fu_flashrom_device_set_programmer_args	(FuFlashromDevice *self,
							 const gchar *args);
gsize		 fu_flashrom_device_get_flash_size	(FuFlashromDevice *self);
struct flashrom_flashctx *fu_flashrom_device_get_flashctx (FuFlashromDevice *self);
