/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WAC_DEVICE (fu_wac_device_get_type ())
G_DECLARE_FINAL_TYPE (FuWacDevice, fu_wac_device, FU, WAC_DEVICE, FuHidDevice)

gboolean	 fu_wac_device_get_feature_report	(FuWacDevice	*self,
							 guint8		*buf,
							 gsize		 bufsz,
							 FuHidDeviceFlags flags,
							 GError		**error);
gboolean	 fu_wac_device_set_feature_report	(FuWacDevice	*self,
							 guint8		*buf,
							 gsize		 bufsz,
							 FuHidDeviceFlags flags,
							 GError		**error);
