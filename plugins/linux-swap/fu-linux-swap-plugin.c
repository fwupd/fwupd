/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-linux-swap-plugin.h"
#include "fu-linux-swap.h"

struct _FuLinuxSwapPlugin {
	FuPlugin parent_instance;
	GFile *file;
	GFileMonitor *monitor;
};

G_DEFINE_TYPE(FuLinuxSwapPlugin, fu_linux_swap_plugin, FU_TYPE_PLUGIN)

static void
fu_linux_swap_plugin_changed_cb(GFileMonitor *monitor,
				GFile *file,
				GFile *other_file,
				GFileMonitorEvent event_type,
				gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN(user_data);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_security_changed(ctx);
}

static gboolean
fu_linux_swap_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuLinuxSwapPlugin *self = FU_LINUX_SWAP_PLUGIN(plugin);
	g_autofree gchar *fn = NULL;
	g_autofree gchar *procfs = NULL;

	procfs = fu_path_from_kind(FU_PATH_KIND_PROCFS);
	fn = g_build_filename(procfs, "swaps", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Kernel doesn't offer swap support.");
		return FALSE;
	}
	self->file = g_file_new_for_path(fn);
	self->monitor = g_file_monitor(self->file, G_FILE_MONITOR_NONE, NULL, error);
	if (self->monitor == NULL)
		return FALSE;
	g_signal_connect(G_FILE_MONITOR(self->monitor),
			 "changed",
			 G_CALLBACK(fu_linux_swap_plugin_changed_cb),
			 plugin);
	return TRUE;
}

static void
fu_linux_swap_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuLinuxSwapPlugin *self = FU_LINUX_SWAP_PLUGIN(plugin);
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autoptr(FuLinuxSwap) swap = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	if (self->file == NULL)
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED);
	fu_security_attrs_append(attrs, attr);

	/* load list of swaps */
	if (!g_file_load_contents(self->file, NULL, &buf, &bufsz, NULL, &error_local)) {
		g_autofree gchar *fn = g_file_get_path(self->file);
		g_warning("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	swap = fu_linux_swap_new(buf, bufsz, &error_local);
	if (swap == NULL) {
		g_autofree gchar *fn = g_file_get_path(self->file);
		g_warning("could not parse %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* none configured */
	if (!fu_linux_swap_get_enabled(swap)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* add security attribute */
	if (!fu_linux_swap_get_encrypted(swap)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_linux_swap_plugin_init(FuLinuxSwapPlugin *self)
{
}

static void
fu_linux_swap_finalize(GObject *obj)
{
	FuLinuxSwapPlugin *self = FU_LINUX_SWAP_PLUGIN(obj);
	if (self->file != NULL)
		g_object_unref(self->file);
	if (self->monitor != NULL) {
		g_file_monitor_cancel(self->monitor);
		g_object_unref(self->monitor);
	}
	G_OBJECT_CLASS(fu_linux_swap_plugin_parent_class)->finalize(obj);
}

static void
fu_linux_swap_plugin_class_init(FuLinuxSwapPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_linux_swap_finalize;
	plugin_class->startup = fu_linux_swap_plugin_startup;
	plugin_class->add_security_attrs = fu_linux_swap_plugin_add_security_attrs;
}
