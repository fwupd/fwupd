/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_WAC_DEVICE (fu_wac_device_get_type ())
G_DECLARE_FINAL_TYPE (FuWacDevice, fu_wac_device, FU, WAC_DEVICE, FuUsbDevice)

typedef enum {
	FU_WAC_DEVICE_FEATURE_FLAG_NONE			= 0,
	FU_WAC_DEVICE_FEATURE_FLAG_ALLOW_TRUNC		= 1 << 0,
	FU_WAC_DEVICE_FEATURE_FLAG_NO_DEBUG		= 1 << 1,
	FU_WAC_DEVICE_FEATURE_FLAG_LAST
} FuWacDeviceFeatureFlags;

gboolean	 fu_wac_device_update_reset		(FuWacDevice	*self,
							 GError		**error);
gboolean	 fu_wac_device_get_feature_report	(FuWacDevice	*self,
							 guint8		*buf,
							 gsize		 bufsz,
							 FuWacDeviceFeatureFlags flags,
							 GError		**error);
gboolean	 fu_wac_device_set_feature_report	(FuWacDevice	*self,
							 guint8		*buf,
							 gsize		 bufsz,
							 FuWacDeviceFeatureFlags flags,
							 GError		**error);
