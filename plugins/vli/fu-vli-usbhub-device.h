/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#include "fu-vli-device.h"
#include "fu-vli-usbhub-i2c-common.h"

#define FU_TYPE_VLI_USBHUB_DEVICE (fu_vli_usbhub_device_get_type ())
G_DECLARE_FINAL_TYPE (FuVliUsbhubDevice, fu_vli_usbhub_device, FU, VLI_USBHUB_DEVICE, FuVliDevice)

struct _FuVliUsbhubDeviceClass
{
	FuVliDeviceClass	parent_class;
};

gboolean	 fu_vli_usbhub_device_i2c_read		(FuVliUsbhubDevice *self,
							 guint8		 cmd,
							 guint8		*buf,
							 gsize		 bufsz,
							 GError		**error);
gboolean	 fu_vli_usbhub_device_i2c_read_status	(FuVliUsbhubDevice *self,
							 FuVliUsbhubI2cStatus *status,
							 GError		**error);
gboolean	 fu_vli_usbhub_device_i2c_write		(FuVliUsbhubDevice *self,
							 guint8		 cmd,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
gboolean	 fu_vli_usbhub_device_i2c_write_data	(FuVliUsbhubDevice *self,
							 guint8		 skip_s,
							 guint8		 skip_p,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
