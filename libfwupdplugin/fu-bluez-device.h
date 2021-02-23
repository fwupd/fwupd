/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_BLUEZ_DEVICE (fu_bluez_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuBluezDevice, fu_bluez_device, FU, BLUEZ_DEVICE, FuDevice)

struct _FuBluezDeviceClass
{
	FuDeviceClass	parent_class;
	gpointer	__reserved[31];
};

GByteArray		*fu_bluez_device_read		(FuBluezDevice	*self,
							 const gchar	*uuid,
							 GError		**error);
gchar			*fu_bluez_device_read_string	(FuBluezDevice	*self,
							 const gchar	*uuid,
							 GError		**error);
gboolean		 fu_bluez_device_write		(FuBluezDevice	*self,
							 const gchar	*uuid,
							 GByteArray	*buf,
							 GError		**error);
