/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ccgx-dmc-device.h"
#include "fu-ccgx-dmc-firmware.h"
#include "fu-ccgx-firmware.h"
#include "fu-ccgx-hid-device.h"
#include "fu-ccgx-hpi-device.h"

static void
fu_plugin_ccgx_init(FuPlugin *plugin)
{
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_CCGX_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_CCGX_DMC_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CCGX_HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CCGX_HPI_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CCGX_DMC_DEVICE);
}

static void
fu_plugin_ccgx_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "CcgxFlashRowSize");
	fu_context_add_quirk_key(ctx, "CcgxFlashSize");
	fu_context_add_quirk_key(ctx, "CcgxImageKind");
	fu_context_add_quirk_key(ctx, "CcgxDmcTriggerCode");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_ccgx_load;
	vfuncs->init = fu_plugin_ccgx_init;
}
