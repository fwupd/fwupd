/*
 * Copyright (C) 2023 Ling.Chen <ling.chen@algoltek.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_USB_FIRMWARE (fu_algoltek_usb_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekUsbFirmware,
		     fu_algoltek_usb_firmware,
		     FU,
		     ALGOLTEK_USB_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_algoltek_usb_firmware_new(void);
