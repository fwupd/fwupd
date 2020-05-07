/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-linux-swap.h"

struct FuPluginData {
	GFileMonitor		*monitor;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->monitor != NULL)
		g_object_unref (data->monitor);
}

static void
fu_plugin_linux_swap_changed_cb (GFileMonitor *monitor,
				 GFile *file,
				 GFile *other_file,
				 GFileMonitorEvent event_type,
				 gpointer user_data)
{
	g_debug ("swap changed");
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *procfs = NULL;
	g_autoptr(FuLinuxSwap) swap = NULL;
	g_autoptr(GFile) file = NULL;

	procfs = fu_common_get_path (FU_PATH_KIND_PROCFS);
	fn = g_build_filename (procfs, "swaps", NULL);
	file = g_file_new_for_path (fn);
	data->monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, error);
	if (data->monitor == NULL)
		return FALSE;
	g_signal_connect (data->monitor, "changed",
			  G_CALLBACK (fu_plugin_linux_swap_changed_cb), plugin);

	/* load list of linux_swaps */
	if (!g_file_get_contents (fn, &buf, &bufsz, error)) {
		g_prefix_error (error, "could not open %s: ", fn);
		return FALSE;
	}
	swap = fu_linux_swap_new (buf, bufsz, error);
	if (swap == NULL) {
		g_prefix_error (error, "could not parse %s: ", fn);
		return FALSE;
	}
	g_debug ("swap %s and %s",
		 fu_linux_swap_get_enabled (swap) ? "enabled" : "disabled",
		 fu_linux_swap_get_encrypted (swap) ? "encrypted" : "unencrypted");
	return TRUE;
}
