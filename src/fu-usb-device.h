/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_USB_DEVICE_H
#define __FU_USB_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_USB_DEVICE (fu_usb_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuUsbDevice, fu_usb_device, FU, USB_DEVICE, FuDevice)

struct _FuUsbDeviceClass
{
	FuDeviceClass	parent_class;
	gboolean	 (*open)		(FuUsbDevice		*device,
						 GError			**error);
	gboolean	 (*close)		(FuUsbDevice		*device,
						 GError			**error);
	gboolean	 (*probe)		(FuUsbDevice		*device,
						 GError			**error);
	gpointer	__reserved[28];
};

FuDevice	*fu_usb_device_new			(GUsbDevice	*usb_device);
GUsbDevice	*fu_usb_device_get_dev			(FuUsbDevice	*device);
void		 fu_usb_device_set_dev			(FuUsbDevice	*device,
							 GUsbDevice	*usb_device);
gboolean	 fu_usb_device_open			(FuUsbDevice	*device,
							 GError		**error);
gboolean	 fu_usb_device_close			(FuUsbDevice	*device,
							 GError		**error);
gboolean	 fu_usb_device_probe			(FuUsbDevice	*device,
							 GError		**error);
gboolean	 fu_usb_device_is_open			(FuUsbDevice	*device);

G_END_DECLS

#endif /* __FU_USB_DEVICE_H */
