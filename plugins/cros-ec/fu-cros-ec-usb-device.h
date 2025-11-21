/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_CROS_EC_SETUP_RETRY_CNT	  5
#define FU_CROS_EC_MAX_BLOCK_XFER_RETRIES 10

#define FU_TYPE_CROS_EC_USB_DEVICE (fu_cros_ec_usb_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCrosEcUsbDevice,
			 fu_cros_ec_usb_device,
			 FU,
			 CROS_EC_USB_DEVICE,
			 FuUsbDevice)

struct _FuCrosEcUsbDeviceClass {
	FuUsbDeviceClass parent_class;
};

#define FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN		     "ro-written"
#define FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN		     "rw-written"
#define FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO	     "rebooting-to-ro"
#define FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL		     "special"
#define FU_CROS_EC_USB_DEVICE_FLAG_CMD_BLOCK_DIGEST_REQUIRED "cmd-block-digest-required"
