/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-cpu-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	gsize length;
	g_autofree gchar *data = NULL;
	g_auto(GStrv) lines = NULL;

	if (!g_file_get_contents ("/proc/cpuinfo", &data, &length, error))
		return FALSE;

	lines = g_strsplit (data, "\n\n", 0);
	for (guint i = 0; lines[i] != NULL; i++) {
		g_autoptr(FuCpuDevice) dev = NULL;
		if (strlen (lines[i]) == 0)
			continue;
		dev = fu_cpu_device_new (lines[i]);
		if (!fu_device_setup (FU_DEVICE (dev), error))
			return FALSE;
		fu_plugin_device_add (plugin, FU_DEVICE (dev));
	}

	return TRUE;
}
