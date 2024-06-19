/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-hid-descriptor.h"
#include "fu-usb-device.h"

#define FU_TYPE_HID_DEVICE (fu_hid_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuHidDevice, fu_hid_device, FU, HID_DEVICE, FuUsbDevice)

struct _FuHidDeviceClass {
	FuUsbDeviceClass parent_class;
};

/**
 * FuHidDeviceFlags:
 * @FU_HID_DEVICE_FLAG_NONE:			No flags set
 * @FU_HID_DEVICE_FLAG_ALLOW_TRUNC:		Allow truncated reads and writes
 * @FU_HID_DEVICE_FLAG_IS_FEATURE:		Use %FU_HID_REPORT_TYPE_FEATURE for wValue
 * @FU_HID_DEVICE_FLAG_RETRY_FAILURE:		Retry up to 10 times on failure
 * @FU_HID_DEVICE_FLAG_NO_KERNEL_UNBIND:	Do not unbind the kernel driver on open
 * @FU_HID_DEVICE_FLAG_NO_KERNEL_REBIND:	Do not rebind the kernel driver on close
 * @FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER:	Use interrupt transfers, not control transfers
 * @FU_HID_DEVICE_FLAG_AUTODETECT_EPS:		Autodetect interface endpoints
 *
 * Flags used when calling fu_hid_device_get_report() and fu_hid_device_set_report().
 **/
typedef enum {
	FU_HID_DEVICE_FLAG_NONE = 0,
	FU_HID_DEVICE_FLAG_ALLOW_TRUNC = 1 << 0,
	FU_HID_DEVICE_FLAG_IS_FEATURE = 1 << 1,
	FU_HID_DEVICE_FLAG_RETRY_FAILURE = 1 << 2,
	FU_HID_DEVICE_FLAG_NO_KERNEL_UNBIND = 1 << 3,
	FU_HID_DEVICE_FLAG_NO_KERNEL_REBIND = 1 << 4,
	FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER = 1 << 5,
	FU_HID_DEVICE_FLAG_AUTODETECT_EPS = 1 << 6,
	/*< private >*/
	FU_HID_DEVICE_FLAG_LAST
} FuHidDeviceFlags;

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
