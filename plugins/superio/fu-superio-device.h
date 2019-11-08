/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_SUPERIO_DEVICE (fu_superio_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuSuperioDevice, fu_superio_device, FU, SUPERIO_DEVICE, FuUdevDevice)

struct _FuSuperioDeviceClass
{
	FuUdevDeviceClass	parent_class;
	gboolean		 (*setup)	(FuSuperioDevice	*self,
						 GError		**error);
};

gboolean	 fu_superio_device_ec_read	(FuSuperioDevice	*self,
						 guint8			*data,
						 GError			**error);
gboolean	 fu_superio_device_ec_write0	(FuSuperioDevice	*self,
						 guint8			 data,
						 GError			**error);
gboolean	 fu_superio_device_ec_write1	(FuSuperioDevice	*self,
						 guint8			 data,
						 GError			**error);
gboolean	 fu_superio_device_ec_get_param	(FuSuperioDevice	*self,
						 guint8			 param,
						 guint8			*data,
						 GError			**error);
gboolean	 fu_superio_device_regval	(FuSuperioDevice	*self,
						 guint8			 addr,
						 guint8			*data,
						 GError			**error);
gboolean	 fu_superio_device_regval16	(FuSuperioDevice	*self,
						 guint8			 addr,
						 guint16		*data,
						 GError			**error);
gboolean	 fu_superio_device_regwrite	(FuSuperioDevice	*self,
						 guint8			 addr,
						 guint8			 data,
						 GError			**error);
