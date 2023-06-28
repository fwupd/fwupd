/*
 * Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_USBHUB_PD_FIRMWARE (fu_genesys_usbhub_pd_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysUsbhubPdFirmware,
		     fu_genesys_usbhub_pd_firmware,
		     FU,
		     GENESYS_USBHUB_PD_FIRMWARE,
		     FuFirmware)
