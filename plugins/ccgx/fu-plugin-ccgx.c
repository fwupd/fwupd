/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ccgx-firmware.h"
#include "fu-ccgx-hid-device.h"
#include "fu-ccgx-hpi-device.h"
#include "fu-ccgx-dmc-device.h"
#include "fu-ccgx-dmc-firmware.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_CCGX_FIRMWARE);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_CCGX_DMC_FIRMWARE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_CCGX_HID_DEVICE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_CCGX_HPI_DEVICE);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_CCGX_DMC_DEVICE);
	fu_context_add_quirk_key (ctx, "CcgxFlashRowSize");
	fu_context_add_quirk_key (ctx, "CcgxFlashSize");
	fu_context_add_quirk_key (ctx, "CcgxImageKind");
}
