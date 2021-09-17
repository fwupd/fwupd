/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANFP_USB_DEVICE (fu_elanfp_usb_device_get_type())
G_DECLARE_FINAL_TYPE(FuElanfpUsbDevice, fu_elanfp_usb_device, FU, ELANFP_USB_DEVICE, FuUsbDevice)

struct _FuElanfpUsbDeviceClass {
	FuUsbDeviceClass parent_class;
};

gboolean
iap_send_command(GUsbDevice *usb_device,
		 guint8 reqType,
		 guint8 request,
		 guint8 *pbuff,
		 gsize len,
		 GError **error);

gboolean
iap_recv_status(GUsbDevice *usb_device, guint8 *pbuff, gsize len, GError **error);

gboolean
run_iap_process(FuElanfpUsbDevice *self, FuFirmware *firmware, GError **error);
