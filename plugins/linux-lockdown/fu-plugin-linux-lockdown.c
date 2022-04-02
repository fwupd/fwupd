/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

typedef enum {
	FU_PLUGIN_LINUX_LOCKDOWN_UNKNOWN,
	FU_PLUGIN_LINUX_LOCKDOWN_INVALID,
	FU_PLUGIN_LINUX_LOCKDOWN_NONE,
	FU_PLUGIN_LINUX_LOCKDOWN_INTEGRITY,
	FU_PLUGIN_LINUX_LOCKDOWN_CONFIDENTIALITY,
} FuPluginLinuxLockdown;

struct FuPluginData {
	GFile *file;
	GFileMonitor *monitor;
	FuPluginLinuxLockdown lockdown;
};

static void
fu_plugin_linux_lockdown_init(FuPlugin *plugin)
{
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
}

static void
fu_plugin_linux_lockdown_destroy(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	if (data->file != NULL)
		g_object_unref(data->file);
	if (data->monitor != NULL) {
		g_file_monitor_cancel(data->monitor);
		g_object_unref(data->monitor);
	}
}

static const gchar *
fu_plugin_linux_lockdown_to_string(FuPluginLinuxLockdown lockdown)
{
	if (lockdown == FU_PLUGIN_LINUX_LOCKDOWN_NONE)
		return "none";
	if (lockdown == FU_PLUGIN_LINUX_LOCKDOWN_INTEGRITY)
		return "integrity";
	if (lockdown == FU_PLUGIN_LINUX_LOCKDOWN_CONFIDENTIALITY)
		return "confidentiality";
	if (lockdown == FU_PLUGIN_LINUX_LOCKDOWN_INVALID)
		return "invalid";
	return NULL;
}

static void
fu_plugin_linux_lockdown_rescan(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;

	/* load file */
	if (!g_file_load_contents(data->file, NULL, &buf, &bufsz, NULL, NULL)) {
		data->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_INVALID;
	} else if (g_strstr_len(buf, bufsz, "[none]") != NULL) {
		data->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_NONE;
	} else if (g_strstr_len(buf, bufsz, "[integrity]") != NULL) {
		data->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_INTEGRITY;
	} else if (g_strstr_len(buf, bufsz, "[confidentiality]") != NULL) {
		data->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_CONFIDENTIALITY;
	} else {
		data->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_UNKNOWN;
	}

	/* update metadata */
	fu_plugin_add_report_metadata(plugin,
				      "LinuxLockdown",
				      fu_plugin_linux_lockdown_to_string(data->lockdown));
}

static void
fu_plugin_linux_lockdown_changed_cb(GFileMonitor *monitor,
				    GFile *file,
				    GFile *other_file,
				    GFileMonitorEvent event_type,
				    gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN(user_data);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_plugin_linux_lockdown_rescan(plugin);
	fu_context_security_changed(ctx);
}

static gboolean
fu_plugin_linux_lockdown_startup(FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autofree gchar *path = NULL;
	g_autofree gchar *fn = NULL;

	path = fu_common_get_path(FU_PATH_KIND_SYSFSDIR_SECURITY);
	fn = g_build_filename(path, "lockdown", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Kernel doesn't offer lockdown support.");
		return FALSE;
	}
	data->file = g_file_new_for_path(fn);
	data->monitor = g_file_monitor(data->file, G_FILE_MONITOR_NONE, NULL, error);
	if (data->monitor == NULL)
		return FALSE;
	g_signal_connect(G_FILE_MONITOR(data->monitor),
			 "changed",
			 G_CALLBACK(fu_plugin_linux_lockdown_changed_cb),
			 plugin);
	fu_plugin_linux_lockdown_rescan(plugin);
	return TRUE;
}

static void
fu_plugin_linux_lockdown_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attrs_append(attrs, attr);

	if (data->lockdown == FU_PLUGIN_LINUX_LOCKDOWN_UNKNOWN) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return;
	}

	/* load file */
	if (data->lockdown == FU_PLUGIN_LINUX_LOCKDOWN_INVALID) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (data->lockdown == FU_PLUGIN_LINUX_LOCKDOWN_NONE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_linux_lockdown_init;
	vfuncs->destroy = fu_plugin_linux_lockdown_destroy;
	vfuncs->startup = fu_plugin_linux_lockdown_startup;
	vfuncs->add_security_attrs = fu_plugin_linux_lockdown_add_security_attrs;
}
