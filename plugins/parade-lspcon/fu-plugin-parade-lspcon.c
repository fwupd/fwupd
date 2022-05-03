/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-parade-lspcon-device.h"

static void
fu_plugin_parade_lspcon_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "ParadeLspconAuxDeviceName");
}

static void
fu_plugin_parade_lspcon_init(FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem(plugin, "i2c");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PARADE_LSPCON_DEVICE);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->load = fu_plugin_parade_lspcon_load;
	vfuncs->init = fu_plugin_parade_lspcon_init;
}
