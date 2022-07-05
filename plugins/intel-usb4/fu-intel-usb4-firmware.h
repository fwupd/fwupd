/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-intel-usb4-nvm.h"

#define FU_TYPE_INTEL_USB4_FIRMWARE (fu_intel_usb4_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuIntelUsb4Firmware,
		     fu_intel_usb4_firmware,
		     FU,
		     INTEL_USB4_FIRMWARE,
		     FuIntelUsb4Nvm)

FuFirmware *
fu_intel_usb4_firmware_new(void);
