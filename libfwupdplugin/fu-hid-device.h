/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-hid-descriptor.h"
#include "fu-hid-struct.h"
#include "fu-usb-device.h"

#define FU_TYPE_HID_DEVICE (fu_hid_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuHidDevice, fu_hid_device, FU, HID_DEVICE, FuUsbDevice)

struct _FuHidDeviceClass {
	FuUsbDeviceClass parent_class;
};

void
fu_hid_device_add_flag(FuHidDevice *self, FuHidDeviceFlags flag) G_GNUC_NON_NULL(1);
void
fu_hid_device_set_interface(FuHidDevice *self, guint8 interface_number) G_GNUC_NON_NULL(1);
guint8
fu_hid_device_get_interface(FuHidDevice *self) G_GNUC_NON_NULL(1);
void
fu_hid_device_set_ep_addr_in(FuHidDevice *self, guint8 ep_addr_in) G_GNUC_NON_NULL(1);
guint8
fu_hid_device_get_ep_addr_in(FuHidDevice *self) G_GNUC_NON_NULL(1);
void
fu_hid_device_set_ep_addr_out(FuHidDevice *self, guint8 ep_addr_out) G_GNUC_NON_NULL(1);
guint8
fu_hid_device_get_ep_addr_out(FuHidDevice *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_hid_device_parse_descriptors(FuHidDevice *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_hid_device_set_report(FuHidDevice *self,
			 guint8 value,
			 guint8 *buf,
			 gsize bufsz,
			 guint timeout,
			 FuHidDeviceFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_hid_device_get_report(FuHidDevice *self,
			 guint8 value,
			 guint8 *buf,
			 gsize bufsz,
			 guint timeout,
			 FuHidDeviceFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
