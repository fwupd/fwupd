/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

static gboolean
fu_linux_lockdown_plugin_ensure_security_attr_flags(FuLinuxLockdownPlugin *self,
						    FwupdSecurityAttr *attr,
						    GError **error)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	const gchar *value;
	gboolean secureboot_enabled = FALSE;
	g_autoptr(GHashTable) cmdline = NULL;
	g_autoptr(GHashTable) config = NULL;

	/* get current args */
	cmdline = fu_kernel_get_cmdline(error);
	if (cmdline == NULL)
		return FALSE;

	/* can we modify the args */
	if (!fu_kernel_check_cmdline_mutable(error))
		return FALSE;

	/* get build flags */
	config = fu_kernel_get_config(error);
	if (config == NULL)
		return FALSE;
	if (!g_hash_table_contains(config, "CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "config does not have CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE");
		return FALSE;
	}

	/* we cannot change this */
	if (!fu_efivars_get_secure_boot(efivars, &secureboot_enabled, error))
		return FALSE;
	if (g_hash_table_contains(config, "CONFIG_LOCK_DOWN_IN_EFI_SECURE_BOOT") &&
	    secureboot_enabled) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "kernel lockdown cannot be changed when secure boot is enabled");
		return FALSE;
	}

	/* set the current and target values */
	value = g_hash_table_lookup(cmdline, "lockdown");
	fwupd_security_attr_set_kernel_current_value(attr, value);
	if (g_strcmp0(value, "integrity") != 0) {
		fwupd_security_attr_set_kernel_target_value(attr, "lockdown=integrity");
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_CAN_FIX);
	} else {
		fwupd_security_attr_set_kernel_target_value(attr, "lockdown=none");
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_CAN_UNDO);
	}
	return TRUE;
}

static void
fu_linux_lockdown_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuLinuxLockdownPlugin *self = FU_LINUX_LOCKDOWN_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* we might be able to fix this */
	if (!fu_linux_lockdown_plugin_ensure_security_attr_flags(self, attr, &error_local))
		g_info("failed to ensure attribute fix flags: %s", error_local->message);

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
	fwupd_codec_string_append(str,
				  idt,
				  "Lockdown",
				  fu_linux_lockdown_to_string(self->lockdown));
}

static void
fu_linux_lockdown_plugin_init(FuLinuxLockdownPlugin *self)
{
}

static void
fu_linux_lockdown_plugin_finalize(GObject *obj)
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
fu_linux_lockdown_plugin_fix_host_security_attr(FuPlugin *plugin,
						FwupdSecurityAttr *attr,
						GError **error)
{
	return fu_kernel_add_cmdline_arg("lockdown=integrity", error);
}

static gboolean
fu_linux_lockdown_plugin_undo_host_security_attr(FuPlugin *plugin,
						 FwupdSecurityAttr *attr,
						 GError **error)
{
	return fu_kernel_remove_cmdline_arg("lockdown=integrity", error);
}

static void
fu_linux_lockdown_plugin_class_init(FuLinuxLockdownPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_linux_lockdown_plugin_finalize;
	plugin_class->to_string = fu_linux_lockdown_plugin_to_string;
	plugin_class->startup = fu_linux_lockdown_plugin_startup;
	plugin_class->add_security_attrs = fu_linux_lockdown_plugin_add_security_attrs;
	plugin_class->fix_host_security_attr = fu_linux_lockdown_plugin_fix_host_security_attr;
	plugin_class->undo_host_security_attr = fu_linux_lockdown_plugin_undo_host_security_attr;
}
