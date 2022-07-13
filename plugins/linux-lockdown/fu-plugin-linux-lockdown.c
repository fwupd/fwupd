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
	FuPluginData *priv = fu_plugin_get_data(plugin);
	if (priv->file != NULL)
		g_object_unref(priv->file);
	if (priv->monitor != NULL) {
		g_file_monitor_cancel(priv->monitor);
		g_object_unref(priv->monitor);
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
	FuPluginData *priv = fu_plugin_get_data(plugin);
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;

	/* load file */
	if (!g_file_load_contents(priv->file, NULL, &buf, &bufsz, NULL, NULL)) {
		priv->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_INVALID;
	} else if (g_strstr_len(buf, bufsz, "[none]") != NULL) {
		priv->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_NONE;
	} else if (g_strstr_len(buf, bufsz, "[integrity]") != NULL) {
		priv->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_INTEGRITY;
	} else if (g_strstr_len(buf, bufsz, "[confidentiality]") != NULL) {
		priv->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_CONFIDENTIALITY;
	} else {
		priv->lockdown = FU_PLUGIN_LINUX_LOCKDOWN_UNKNOWN;
	}

	/* update metadata */
	fu_plugin_add_report_metadata(plugin,
				      "LinuxLockdown",
				      fu_plugin_linux_lockdown_to_string(priv->lockdown));
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
fu_plugin_linux_lockdown_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autofree gchar *path = NULL;
	g_autofree gchar *fn = NULL;

	path = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_SECURITY);
	fn = g_build_filename(path, "lockdown", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Kernel doesn't offer lockdown support.");
		return FALSE;
	}
	priv->file = g_file_new_for_path(fn);
	priv->monitor = g_file_monitor(priv->file, G_FILE_MONITOR_NONE, NULL, error);
	if (priv->monitor == NULL)
		return FALSE;
	g_signal_connect(G_FILE_MONITOR(priv->monitor),
			 "changed",
			 G_CALLBACK(fu_plugin_linux_lockdown_changed_cb),
			 plugin);
	fu_plugin_linux_lockdown_rescan(plugin);
	return TRUE;
}

static void
fu_plugin_linux_lockdown_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attrs_append(attrs, attr);

	if (priv == NULL || priv->lockdown == FU_PLUGIN_LINUX_LOCKDOWN_UNKNOWN) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return;
	}

	/* load file */
	if (priv->lockdown == FU_PLUGIN_LINUX_LOCKDOWN_INVALID) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (priv->lockdown == FU_PLUGIN_LINUX_LOCKDOWN_NONE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
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
