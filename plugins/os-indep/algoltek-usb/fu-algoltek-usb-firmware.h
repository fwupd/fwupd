/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_USB_FIRMWARE (fu_algoltek_usb_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekUsbFirmware,
		     fu_algoltek_usb_firmware,
		     FU,
		     ALGOLTEK_USB_FIRMWARE,
		     FuFirmware)
