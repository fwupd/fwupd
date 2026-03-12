/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PARADE_USBHUB_FIRMWARE (fu_parade_usbhub_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuParadeUsbhubFirmware,
		     fu_parade_usbhub_firmware,
		     FU,
		     PARADE_USBHUB_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_parade_usbhub_firmware_new(void);
