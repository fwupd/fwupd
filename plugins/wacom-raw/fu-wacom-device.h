/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wacom-common.h"
#include "fu-udev-device.h"

#define FU_TYPE_WACOM_DEVICE (fu_wacom_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuWacomDevice, fu_wacom_device, FU, WACOM_DEVICE, FuUdevDevice)

struct _FuWacomDeviceClass
{
	FuUdevDeviceClass	parent_class;
	gboolean		 (*write_firmware)	(FuDevice	*self,
							 GPtrArray	*chunks,
							 GError		**error);
};

typedef enum {
	FU_WACOM_DEVICE_CMD_FLAG_NONE			= 0,
	FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING	= 1 << 0,
	FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK		= 1 << 1,
} FuWacomDeviceCmdFlags;

gboolean	 fu_wacom_device_set_feature	(FuWacomDevice	*self,
						 const guint8	*data,
						 guint		 datasz,
						 GError		**error);
gboolean	 fu_wacom_device_get_feature	(FuWacomDevice	*self,
						 guint8		*data,
						 guint		 datasz,
						 GError		**error);
gboolean	 fu_wacom_device_cmd		(FuWacomDevice	*self,
						 FuWacomRawRequest *req,
						 FuWacomRawResponse *rsp,
						 gulong		 delay_us,
						 FuWacomDeviceCmdFlags flags,
						 GError 	**error);
gboolean	 fu_wacom_device_erase_all	(FuWacomDevice	*self,
						 GError		**error);
gboolean	 fu_wacom_device_check_mpu	(FuWacomDevice	*self,
						 GError		**error);
guint		 fu_wacom_device_get_block_sz	(FuWacomDevice	*self);
guint		 fu_wacom_device_get_base_addr	(FuWacomDevice	*self);
