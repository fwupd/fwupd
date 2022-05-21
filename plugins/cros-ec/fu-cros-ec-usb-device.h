/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CROS_EC_USB_DEVICE (fu_cros_ec_usb_device_get_type())
G_DECLARE_FINAL_TYPE(FuCrosEcUsbDevice, fu_cros_ec_usb_device, FU, CROS_EC_USB_DEVICE, FuUsbDevice)
