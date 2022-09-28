/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"
#include "fu-usb-device.h"

#define FU_TYPE_USB_DEVICE_DS20 (fu_usb_device_ds20_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUsbDeviceDs20, fu_usb_device_ds20, FU, USB_DEVICE_DS20, FuFirmware)

struct _FuUsbDeviceDs20Class {
	FuFirmwareClass parent_class;
	gboolean (*parse)(FuUsbDeviceDs20 *self, GBytes *blob, FuUsbDevice *device, GError **error)
	    G_GNUC_WARN_UNUSED_RESULT;
};

void
fu_usb_device_ds20_set_version_lowest(FuUsbDeviceDs20 *self, guint32 version_lowest);
gboolean
fu_usb_device_ds20_apply_to_device(FuUsbDeviceDs20 *self, FuUsbDevice *device, GError **error);
