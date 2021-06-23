/*
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_EC_DEVICE (fu_ec_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuEcDevice, fu_ec_device, FU, EC_DEVICE, FuUdevDevice)

struct _FuEcDeviceClass
{
	FuUdevDeviceClass	parent_class;
};

gboolean	fu_ec_device_write_cmd	(FuEcDevice	*self,
					 guint8		 cmd,
					 GError		**error);
gboolean	fu_ec_device_read	(FuEcDevice	*self,
					 guint8		*data,
					 GError		**error);
gboolean	fu_ec_device_write	(FuEcDevice	*self,
					 guint8		 data,
					 GError		**error);
gboolean	fu_ec_device_read_reg	(FuEcDevice	*self,
					 guint8		 address,
					 guint8		*data,
					 GError		**error);
gboolean	fu_ec_device_write_reg	(FuEcDevice	*self,
					 guint8		 address,
					 guint8		 data,
					 GError		**error);
