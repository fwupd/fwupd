/*
 * Copyright 2025 Hamed Elgizery
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cros-ec-hammer-touchpad.h"
#include "fu-cros-ec-usb-device.h"

#define FU_TYPE_CROS_EC_USB_HAMMER (fu_cros_ec_usb_hammer_get_type())
G_DECLARE_FINAL_TYPE(FuCrosEcUsbHammer,
		     fu_cros_ec_usb_hammer,
		     FU,
		     CROS_EC_USB_HAMMER,
		     FuCrosEcUsbDevice)

gboolean
fu_cros_ec_usb_hammer_write_touchpad_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      FuDevice *tp_device,
					      GError **error);
