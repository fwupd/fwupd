/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_USB_DEVICE (fu_algoltek_usb_device_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekUsbDevice,
		     fu_algoltek_usb_device,
		     FU,
		     ALGOLTEK_USB_DEVICE,
		     FuUsbDevice)
