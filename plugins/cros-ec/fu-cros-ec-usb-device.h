/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cros-ec-hammer-touchpad.h"

#define FU_CROS_EC_SETUP_RETRY_CNT 5

#define FU_TYPE_CROS_EC_USB_DEVICE (fu_cros_ec_usb_device_get_type())
G_DECLARE_FINAL_TYPE(FuCrosEcUsbDevice, fu_cros_ec_usb_device, FU, CROS_EC_USB_DEVICE, FuUsbDevice)

gboolean
fu_cros_ec_usb_device_send_subcommand(FuCrosEcUsbDevice *self,
				      guint16 subcommand,
				      guint8 *cmd_body,
				      gsize body_size,
				      guint8 *resp,
				      gsize *resp_size,
				      gboolean allow_less,
				      GError **error);

gboolean
fu_cros_ec_usb_device_start_request_cb(FuDevice *device, gpointer user_data, GError **error);

gboolean
fu_cros_ec_usb_device_write_touchpad_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      FuDevice *tp_device,
					      GError **error);
