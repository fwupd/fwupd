/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-usb-device.h"

#define FU_TYPE_HID_DEVICE (fu_hid_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuHidDevice, fu_hid_device, FU, HID_DEVICE, FuUsbDevice)

struct _FuHidDeviceClass
{
	FuUsbDeviceClass parent_class;
	gboolean	 (*open)		(FuHidDevice		*device,
						 GError			**error);
	gboolean	 (*close)		(FuHidDevice		*device,
						 GError			**error);
	gpointer	__reserved[29];
};

/**
 * FuHidDeviceFlags:
 * @FU_HID_DEVICE_FLAG_NONE:			No flags set
 * @FU_HID_DEVICE_FLAG_ALLOW_TRUNC:		Allow truncated reads and writes
 * @FU_HID_DEVICE_FLAG_IS_FEATURE:		Use %FU_HID_REPORT_TYPE_FEATURE for wValue
 *
 * Flags used when calling fu_hid_device_get_report() and fu_hid_device_set_report().
 **/
typedef enum {
	FU_HID_DEVICE_FLAG_NONE			= 0,
	FU_HID_DEVICE_FLAG_ALLOW_TRUNC		= 1 << 0,
	FU_HID_DEVICE_FLAG_IS_FEATURE		= 1 << 1,
	FU_HID_DEVICE_FLAG_LAST
} FuHidDeviceFlags;

FuHidDevice	*fu_hid_device_new			(GUsbDevice	*usb_device);
void		 fu_hid_device_set_interface		(FuHidDevice	*self,
							 guint8		 interface);
guint8		 fu_hid_device_get_interface		(FuHidDevice	*self);
gboolean	 fu_hid_device_set_report		(FuHidDevice	*self,
							 guint8		 value,
							 guint8		*buf,
							 gsize		 bufsz,
							 guint		 timeout,
							 FuHidDeviceFlags flags,
							 GError		**error);
gboolean	 fu_hid_device_get_report		(FuHidDevice	*self,
							 guint8		 value,
							 guint8		*buf,
							 gsize		 bufsz,
							 guint		 timeout,
							 FuHidDeviceFlags flags,
							 GError		**error);
