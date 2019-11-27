/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"
#include "fu-udev-device.h"

#define FU_TYPE_USB_DEVICE (fu_usb_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuUsbDevice, fu_usb_device, FU, USB_DEVICE, FuDevice)

/* HID */
#define FU_HID_REPORT_GET				0x01
#define FU_HID_REPORT_SET				0x09

#define FU_HID_REPORT_TYPE_INPUT			0x01
#define FU_HID_REPORT_TYPE_OUTPUT			0x02
#define FU_HID_REPORT_TYPE_FEATURE			0x03

#define FU_HID_FEATURE					0x0300

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

FuUsbDevice	*fu_usb_device_new			(GUsbDevice	*usb_device);
guint16		 fu_usb_device_get_vid			(FuUsbDevice	*self);
guint16		 fu_usb_device_get_pid			(FuUsbDevice	*self);
guint16		 fu_usb_device_get_spec			(FuUsbDevice	*self);
GUsbDevice	*fu_usb_device_get_dev			(FuUsbDevice	*device);
void		 fu_usb_device_set_dev			(FuUsbDevice	*device,
							 GUsbDevice	*usb_device);
gboolean	 fu_usb_device_is_open			(FuUsbDevice	*device);
GUdevDevice	*fu_usb_device_find_udev_device		(FuUsbDevice	*device,
							 GError		**error);
