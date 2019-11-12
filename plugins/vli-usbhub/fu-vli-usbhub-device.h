/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_VLI_USBHUB_DEVICE (fu_vli_usbhub_device_get_type ())
G_DECLARE_FINAL_TYPE (FuVliUsbhubDevice, fu_vli_usbhub_device, FU, VLI_USBHUB_DEVICE, FuUsbDevice)

struct _FuVliUsbhubDeviceClass
{
	FuUsbDeviceClass	parent_class;
};

gboolean	 fu_vli_usbhub_device_spi_erase		(FuVliUsbhubDevice *self,
							 guint32	 addr,
							 gsize		 sz,
							 GError		**error);
gboolean	 fu_vli_usbhub_device_spi_write		(FuVliUsbhubDevice *self,
							 guint32	 address,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
GBytes		*fu_vli_usbhub_device_spi_read		(FuVliUsbhubDevice *self,
							 guint32	 address,
							 gsize		 bufsz,
							 GError		**error);
