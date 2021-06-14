/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cpu-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_BEFORE, "msr");
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	g_autoptr(FuCpuDevice) dev = fu_cpu_device_new ();
	fu_device_set_context (FU_DEVICE (dev), ctx);
	if (!fu_device_probe (FU_DEVICE (dev), error))
		return FALSE;
	if (!fu_device_setup (FU_DEVICE (dev), error))
		return FALSE;
	fu_plugin_cache_add (plugin, "cpu", dev);
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}
