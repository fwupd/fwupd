/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

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
