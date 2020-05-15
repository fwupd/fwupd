/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

struct FuPluginData {
	GFile			*file;
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
	if (data->file != NULL)
		g_object_unref (data->file);
	if (data->monitor != NULL)
		g_object_unref (data->monitor);
}

static void
fu_plugin_linux_tainted_changed_cb (GFileMonitor *monitor,
				 GFile *file,
				 GFile *other_file,
				 GFileMonitorEvent event_type,
				 gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	fu_plugin_security_changed (plugin);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *fn = NULL;
	g_autofree gchar *procfs = NULL;

	procfs = fu_common_get_path (FU_PATH_KIND_PROCFS);
	fn = g_build_filename (procfs, "sys", "kernel", "tainted", NULL);
	data->file = g_file_new_for_path (fn);
	data->monitor = g_file_monitor (data->file, G_FILE_MONITOR_NONE, NULL, error);
	if (data->monitor == NULL)
		return FALSE;
	g_signal_connect (data->monitor, "changed",
			  G_CALLBACK (fu_plugin_linux_tainted_changed_cb), plugin);
	return TRUE;
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new ("org.kernel.CheckTainted");
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_name (attr, "Linux Kernel Taint");
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attrs_append (attrs, attr);

	/* load file */
	if (!g_file_load_contents (data->file, NULL, &buf, &bufsz, NULL, &error_local)) {
		g_autofree gchar *fn = g_file_get_path (data->file);
		g_warning ("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, "Could not open file");
		return;
	}
	if (g_strcmp0 (buf, "0\n") != 0) {
		fwupd_security_attr_set_result (attr, "Tainted");
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}
