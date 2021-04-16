/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-realtek-mst-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);

	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_context_add_quirk_key (ctx, "RealtekMstDpAuxName");

	fu_context_add_udev_subsystem (ctx, "i2c");
	fu_plugin_add_device_gtype (plugin, FU_TYPE_REALTEK_MST_DEVICE);
}
