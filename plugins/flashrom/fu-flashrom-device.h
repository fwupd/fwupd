/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

struct _FuFlashromDeviceClass {
	FuUdevDeviceClass		 parent_class;
};

#define FU_TYPE_FLASHROM_DEVICE (fu_flashrom_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuFlashromDevice, fu_flashrom_device, FU,
			  FLASHROM_DEVICE, FuUdevDevice)

/**
 * FuFlashromDeviceFlags:
 * @FU_FLASHROM_DEVICE_FLAG_NONE:		No flags set
 * @FU_FLASHROM_DEVICE_FLAG_OPEN_PROGRAMMER:	Call open_programmer on FuDevice->open
 */
typedef enum {
	FU_FLASHROM_DEVICE_FLAG_NONE = 0,
	FU_FLASHROM_DEVICE_FLAG_OPEN_PROGRAMMER = 1,
} FuFlashromDeviceFlags;

void		 fu_flashrom_device_set_programmer_name	(FuFlashromDevice *self,
							 const gchar	*name);
const gchar	*fu_flashrom_device_get_programmer_name (FuFlashromDevice *self);
void		 fu_flashrom_device_set_programmer_args	(FuFlashromDevice *self,
							 const gchar	*args);
FuFlashromDeviceFlags fu_flashrom_device_get_flags (FuFlashromDevice *self);
void 		 fu_flashrom_device_set_flags (FuFlashromDevice *self,
					       FuFlashromDeviceFlags flags);
gboolean 	 fu_flashrom_device_open_programmer (FuFlashromDevice *self,
						     GError **error);
void		 fu_flashrom_device_close_programmer (FuFlashromDevice *self);
gsize		 fu_flashrom_device_get_flash_size	(FuFlashromDevice *self);
struct flashrom_flashctx *fu_flashrom_device_get_flashctx (FuFlashromDevice *self);
