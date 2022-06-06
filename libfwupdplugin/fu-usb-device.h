/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#ifdef HAVE_GUSB
#include <gusb.h>
#else
#define GUsbContext GObject
#define GUsbDevice  GObject
#ifndef __GI_SCANNER__
#define G_USB_CHECK_VERSION(a, c, b) 0
#endif
#endif

#include "fu-plugin.h"
#include "fu-udev-device.h"

#define FU_TYPE_USB_DEVICE (fu_usb_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUsbDevice, fu_usb_device, FU, USB_DEVICE, FuDevice)

struct _FuUsbDeviceClass {
	FuDeviceClass parent_class;
	gpointer __reserved[31];
};

FuUsbDevice *
fu_usb_device_new(FuContext *ctx, GUsbDevice *usb_device);
guint16
fu_usb_device_get_vid(FuUsbDevice *self);
guint16
fu_usb_device_get_pid(FuUsbDevice *self);
guint16
fu_usb_device_get_spec(FuUsbDevice *self);
GUsbDevice *
fu_usb_device_get_dev(FuUsbDevice *device);
void
fu_usb_device_set_dev(FuUsbDevice *device, GUsbDevice *usb_device);
gboolean
fu_usb_device_is_open(FuUsbDevice *device);
GUdevDevice *
fu_usb_device_find_udev_device(FuUsbDevice *device, GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_usb_device_set_configuration(FuUsbDevice *device, gint configuration);
void
fu_usb_device_add_interface(FuUsbDevice *device, guint8 number);
