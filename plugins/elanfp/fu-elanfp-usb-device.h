/*
 * Copyright (C) 2021
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

// communication
gboolean
iapSendCommand(GUsbDevice *usb_device,
	       guint8 reqType,
	       guint8 request,
	       guint8 *pbuff,
	       gsize len,
	       GError **error);

gboolean
iapRecvStatus(GUsbDevice *usb_device, guint8 *pbuff, gsize len, GError **error);

// run iap
gboolean
runIapProcess(FuElanfpUsbDevice *self, GBytes *fw, GError **error);
