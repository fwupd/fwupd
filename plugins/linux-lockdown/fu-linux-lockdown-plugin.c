/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-linux-lockdown-plugin.h"
#include "fu-linux-lockdown-struct.h"

struct _FuLinuxLockdownPlugin {
	FuPlugin parent_instance;
	GFile *file;
	GFileMonitor *monitor;
	FuLinuxLockdown lockdown;
};

G_DEFINE_TYPE(FuLinuxLockdownPlugin, fu_linux_lockdown_plugin, FU_TYPE_PLUGIN)

static void
fu_linux_lockdown_plugin_rescan(FuPlugin *plugin)
{
	FuLinuxLockdownPlugin *self = FU_LINUX_LOCKDOWN_PLUGIN(plugin);
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;

	/* load file */
	if (!g_file_load_contents(self->file, NULL, &buf, &bufsz, NULL, NULL)) {
		self->lockdown = FU_LINUX_LOCKDOWN_INVALID;
	} else if (g_strstr_len(buf, bufsz, "[none]") != NULL) {
		self->lockdown = FU_LINUX_LOCKDOWN_NONE;
	} else if (g_strstr_len(buf, bufsz, "[integrity]") != NULL) {
		self->lockdown = FU_LINUX_LOCKDOWN_INTEGRITY;
	} else if (g_strstr_len(buf, bufsz, "[confidentiality]") != NULL) {
		self->lockdown = FU_LINUX_LOCKDOWN_CONFIDENTIALITY;
	} else {
		self->lockdown = FU_LINUX_LOCKDOWN_UNKNOWN;
	}

	/* update metadata */
	fu_plugin_add_report_metadata(plugin,
				      "LinuxLockdown",
				      fu_linux_lockdown_to_string(self->lockdown));
}

static void
fu_linux_lockdown_plugin_changed_cb(GFileMonitor *monitor,
				    GFile *file,
				    GFile *other_file,
				    GFileMonitorEvent event_type,
				    gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN(user_data);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_linux_lockdown_plugin_rescan(plugin);
	fu_context_security_changed(ctx);
}

static gboolean
fu_linux_lockdown_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuLinuxLockdownPlugin *self = FU_LINUX_LOCKDOWN_PLUGIN(plugin);
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
	self->file = g_file_new_for_path(fn);
	self->monitor = g_file_monitor(self->file, G_FILE_MONITOR_NONE, NULL, error);
	if (self->monitor == NULL)
		return FALSE;
	g_signal_connect(G_FILE_MONITOR(self->monitor),
			 "changed",
			 G_CALLBACK(fu_linux_lockdown_plugin_changed_cb),
			 plugin);
	fu_linux_lockdown_plugin_rescan(plugin);
	return TRUE;
}

static void
fu_linux_lockdown_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuLinuxLockdownPlugin *self = FU_LINUX_LOCKDOWN_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	if (self->lockdown == FU_LINUX_LOCKDOWN_UNKNOWN) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return;
	}

	/* load file */
	if (self->lockdown == FU_LINUX_LOCKDOWN_INVALID) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	if (self->lockdown == FU_LINUX_LOCKDOWN_NONE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_linux_lockdown_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuLinuxLockdownPlugin *self = FU_LINUX_LOCKDOWN_PLUGIN(plugin);
	fu_string_append(str, idt, "Lockdown", fu_linux_lockdown_to_string(self->lockdown));
}

static void
fu_linux_lockdown_plugin_init(FuLinuxLockdownPlugin *self)
{
}

static void
fu_linux_lockdown_finalize(GObject *obj)
{
	FuLinuxLockdownPlugin *self = FU_LINUX_LOCKDOWN_PLUGIN(obj);
	if (self->file != NULL)
		g_object_unref(self->file);
	if (self->monitor != NULL) {
		g_file_monitor_cancel(self->monitor);
		g_object_unref(self->monitor);
	}
	G_OBJECT_CLASS(fu_linux_lockdown_plugin_parent_class)->finalize(obj);
}

static gboolean
fu_linux_lockdown_plugin_security_fix(FuPlugin *plugin, FwupdSecurityAttr *attr, GError **error)
{
	g_autofree gchar *grubby = NULL;
	g_autoptr(GHashTable) kernel_param = NULL;

	g_return_val_if_fail(attr != NULL, FALSE);

	grubby = fu_kernel_get_grubby_path(error);
	if (grubby == NULL)
		return FALSE;

	kernel_param = fu_kernel_get_cmdline(error);
	if (kernel_param == NULL)
		return FALSE;

	if (g_hash_table_contains(kernel_param, "lockdown")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "Kernel lockdown has already been enabled.");
		return FALSE;
	}
	return fu_kernel_add_cmdline_arg(grubby, "lockdown=confidentiality", error);
}

static gboolean
fu_linux_lockdown_plugin_security_unfix(FuPlugin *plugin, FwupdSecurityAttr *attr, GError **error)
{
	g_autofree gchar *grubby = NULL;
	g_autoptr(GHashTable) kernel_param = NULL;
	guint flags;

	g_return_val_if_fail(attr != NULL, FALSE);

	grubby = fu_kernel_get_grubby_path(error);
	if (grubby == NULL)
		return FALSE;

	kernel_param = fu_kernel_get_cmdline(error);
	if (kernel_param == NULL)
		return FALSE;

	flags = fwupd_security_attr_get_flags(attr);

	if (flags == FWUPD_SECURITY_ATTR_FLAG_SUCCESS) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOTHING_TO_DO,
		    "Kernel lockdown can't be disabled when secure boot is enabled.");
		return FALSE;
	}

	if (!g_hash_table_contains(kernel_param, "lockdown")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "Can't be reverted since kernel lockdown was disabled.");
		return FALSE;
	}
	return fu_kernel_remove_cmdline_arg(grubby, "lockdown=confidentiality", error);
}

static void
fu_linux_lockdown_plugin_class_init(FuLinuxLockdownPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_linux_lockdown_finalize;
	plugin_class->to_string = fu_linux_lockdown_plugin_to_string;
	plugin_class->startup = fu_linux_lockdown_plugin_startup;
	plugin_class->add_security_attrs = fu_linux_lockdown_plugin_add_security_attrs;
	plugin_class->security_hardening_fix = fu_linux_lockdown_plugin_security_fix;
	plugin_class->security_hardening_unfix = fu_linux_lockdown_plugin_security_unfix;
}
