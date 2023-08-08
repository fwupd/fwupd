/*
 * Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_USBHUB_CODESIGN_FIRMWARE (fu_genesys_usbhub_codesign_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysUsbhubCodesignFirmware,
		     fu_genesys_usbhub_codesign_firmware,
		     FU,
		     GENESYS_USBHUB_CODESIGN_FIRMWARE,
		     FuFirmware)

gint
fu_genesys_usbhub_codesign_firmware_get_codesign(FuGenesysUsbhubCodesignFirmware *self);
