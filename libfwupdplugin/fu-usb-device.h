/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
};

FuUsbDevice *
fu_usb_device_new(FuContext *ctx, GUsbDevice *usb_device) G_GNUC_NON_NULL(1);
guint16
fu_usb_device_get_vid(FuUsbDevice *self) G_GNUC_NON_NULL(1);
guint16
fu_usb_device_get_pid(FuUsbDevice *self) G_GNUC_NON_NULL(1);
guint16
fu_usb_device_get_spec(FuUsbDevice *self) G_GNUC_NON_NULL(1);
GUsbDevice *
fu_usb_device_get_dev(FuUsbDevice *device) G_GNUC_NON_NULL(1);
void
fu_usb_device_set_dev(FuUsbDevice *device, GUsbDevice *usb_device) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_is_open(FuUsbDevice *device) G_GNUC_NON_NULL(1);
GUdevDevice *
fu_usb_device_find_udev_device(FuUsbDevice *device, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
void
fu_usb_device_set_configuration(FuUsbDevice *device, gint configuration) G_GNUC_NON_NULL(1);
void
fu_usb_device_add_interface(FuUsbDevice *device, guint8 number) G_GNUC_NON_NULL(1);
void
fu_usb_device_set_claim_retry_count(FuUsbDevice *self, guint claim_retry_count) G_GNUC_NON_NULL(1);
guint
fu_usb_device_get_claim_retry_count(FuUsbDevice *self) G_GNUC_NON_NULL(1);
void
fu_usb_device_set_open_retry_count(FuUsbDevice *self, guint open_retry_count) G_GNUC_NON_NULL(1);
guint
fu_usb_device_get_open_retry_count(FuUsbDevice *self) G_GNUC_NON_NULL(1);
