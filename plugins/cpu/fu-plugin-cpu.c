/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cpu-device.h"

static void
fu_plugin_cpu_init(FuPlugin *plugin)
{
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_BEFORE, "msr");
}

static gboolean
fu_plugin_cpu_coldplug(FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuCpuDevice) dev = fu_cpu_device_new(ctx);
	if (!fu_device_probe(FU_DEVICE(dev), error))
		return FALSE;
	if (!fu_device_setup(FU_DEVICE(dev), error))
		return FALSE;
	fu_plugin_cache_add(plugin, "cpu", dev);
	fu_plugin_device_add(plugin, FU_DEVICE(dev));
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_cpu_init;
	vfuncs->coldplug = fu_plugin_cpu_coldplug;
}
