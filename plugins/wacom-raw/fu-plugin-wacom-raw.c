/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-wacom-aes-device.h"
#include "fu-wacom-common.h"
#include "fu-wacom-emr-device.h"

static void
fu_plugin_wacom_raw_init(FuPlugin *plugin)
{
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_AES_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WACOM_EMR_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

static void
fu_plugin_wacom_raw_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "WacomI2cFlashBlockSize");
	fu_context_add_quirk_key(ctx, "WacomI2cFlashBaseAddr");
	fu_context_add_quirk_key(ctx, "WacomI2cFlashSize");
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_wacom_raw_load;
	vfuncs->init = fu_plugin_wacom_raw_init;
}
