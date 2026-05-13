/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WACOM_USB_MODULE (fu_wacom_usb_module_get_type())
G_DECLARE_DERIVABLE_TYPE(FuWacomUsbModule, fu_wacom_usb_module, FU, WACOM_USB_MODULE, FuDevice)

struct _FuWacomUsbModuleClass {
	FuDeviceClass parent_class;
};

#define FU_WACOM_USB_MODULE_POLL_INTERVAL 100	/* ms */
#define FU_WACOM_USB_MODULE_START_TIMEOUT 15000 /* ms */
#define FU_WACOM_USB_MODULE_DATA_TIMEOUT  10000 /* ms */
#define FU_WACOM_USB_MODULE_END_TIMEOUT	  10000 /* ms */

gboolean
fu_wacom_usb_module_set_feature(FuWacomUsbModule *self,
				guint8 command,
				GBytes *blob,
				FuProgress *progress,
				guint poll_interval,
				guint busy_timeout,
				GError **error);
