/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WACOM_USB_DEVICE (fu_wacom_usb_device_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbDevice, fu_wacom_usb_device, FU, WACOM_USB_DEVICE, FuHidDevice)

gboolean
fu_wacom_usb_device_get_feature_report(FuWacomUsbDevice *self,
				       guint8 *buf,
				       gsize bufsz,
				       FuHidDeviceFlags flags,
				       GError **error);
gboolean
fu_wacom_usb_device_set_feature_report(FuWacomUsbDevice *self,
				       guint8 *buf,
				       gsize bufsz,
				       FuHidDeviceFlags flags,
				       GError **error);
gboolean
fu_wacom_usb_device_switch_to_flash_loader(FuWacomUsbDevice *self, GError **error);
gboolean
fu_wacom_usb_device_update_reset(FuWacomUsbDevice *self, GError **error);
