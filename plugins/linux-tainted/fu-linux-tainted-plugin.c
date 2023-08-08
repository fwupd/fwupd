/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-linux-tainted-plugin.h"

struct _FuLinuxTaintedPlugin {
	FuPlugin parent_instance;
	GFile *file;
	GFileMonitor *monitor;
};

G_DEFINE_TYPE(FuLinuxTaintedPlugin, fu_linux_tainted_plugin, FU_TYPE_PLUGIN)

#define KERNEL_TAINT_FLAG_PROPRIETARY_MODULE	      (1 << 0)
#define KERNEL_TAINT_FLAG_MODULE_FORCE_LOAD	      (1 << 1)
#define KERNEL_TAINT_FLAG_KERNEL_OUT_OF_SPEC	      (1 << 2)
#define KERNEL_TAINT_FLAG_MODULE_FORCE_UNLOAD	      (1 << 3)
#define KERNEL_TAINT_FLAG_PROCESSOR_MCE		      (1 << 4)
#define KERNEL_TAINT_FLAG_BAD_PAGE		      (1 << 5)
#define KERNEL_TAINT_FLAG_REQUESTED_BY_USERSPACE      (1 << 6)
#define KERNEL_TAINT_FLAG_KERNEL_DIED		      (1 << 7)
#define KERNEL_TAINT_FLAG_ACPI_OVERRIDDEN	      (1 << 8)
#define KERNEL_TAINT_FLAG_KERNEL_ISSUED_WARNING	      (1 << 9)
#define KERNEL_TAINT_FLAG_STAGING_DRIVER_LOADED	      (1 << 10)
#define KERNEL_TAINT_FLAG_FIRMWARE_WORKAROUND_APPLIED (1 << 11)
#define KERNEL_TAINT_FLAG_EXTERNAL_MODULE_LOADED      (1 << 12)
#define KERNEL_TAINT_FLAG_UNSIGNED_MODULE_LOADED      (1 << 13)
#define KERNEL_TAINT_FLAG_SOFT_LOCKUP_OCCURRED	      (1 << 14)
#define KERNEL_TAINT_FLAG_KERNEL_LIVE_PATCHED	      (1 << 15)
#define KERNEL_TAINT_FLAG_AUXILIARY_TAINT	      (1 << 16)
#define KERNEL_TAINT_FLAG_STRUCT_RANDOMIZATION_PLUGIN (1 << 17)
#define KERNEL_TAINT_FLAG_IN_KERNEL_TEST	      (1 << 18)

static void
fu_linux_tainted_plugin_changed_cb(GFileMonitor *monitor,
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
fu_linux_tainted_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuLinuxTaintedPlugin *self = FU_LINUX_TAINTED_PLUGIN(plugin);
	g_autofree gchar *fn = NULL;
	g_autofree gchar *procfs = NULL;

	procfs = fu_path_from_kind(FU_PATH_KIND_PROCFS);
	fn = g_build_filename(procfs, "sys", "kernel", "tainted", NULL);
	self->file = g_file_new_for_path(fn);
	self->monitor = g_file_monitor(self->file, G_FILE_MONITOR_NONE, NULL, error);
	if (self->monitor == NULL)
		return FALSE;
	g_signal_connect(G_FILE_MONITOR(self->monitor),
			 "changed",
			 G_CALLBACK(fu_linux_tainted_plugin_changed_cb),
			 plugin);
	return TRUE;
}

static void
fu_linux_tainted_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuLinuxTaintedPlugin *self = FU_LINUX_TAINTED_PLUGIN(plugin);
	gsize bufsz = 0;
	guint64 value = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED);
	fu_security_attrs_append(attrs, attr);

	/* startup failed */
	if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return;
	}

	/* load file */
	if (!g_file_load_contents(self->file, NULL, &buf, &bufsz, NULL, &error_local)) {
		g_autofree gchar *fn = g_file_get_path(self->file);
		g_warning("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* do not assume NUL terminated */
	str = g_strndup(buf, bufsz);
	if (!fu_strtoull(str, &value, 0, G_MAXUINT64, &error_local)) {
		g_warning("could not parse %s: %s", str, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* only some taint flags are important */
	if ((value & KERNEL_TAINT_FLAG_PROPRIETARY_MODULE) > 0 ||
	    (value & KERNEL_TAINT_FLAG_MODULE_FORCE_LOAD) > 0 ||
	    (value & KERNEL_TAINT_FLAG_MODULE_FORCE_UNLOAD) > 0 ||
	    (value & KERNEL_TAINT_FLAG_STAGING_DRIVER_LOADED) > 0 ||
	    (value & KERNEL_TAINT_FLAG_EXTERNAL_MODULE_LOADED) > 0 ||
	    (value & KERNEL_TAINT_FLAG_UNSIGNED_MODULE_LOADED) > 0 ||
	    (value & KERNEL_TAINT_FLAG_ACPI_OVERRIDDEN) > 0 ||
	    (value & KERNEL_TAINT_FLAG_AUXILIARY_TAINT) > 0) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_TAINTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_linux_tainted_plugin_init(FuLinuxTaintedPlugin *self)
{
}

static void
fu_linux_tainted_finalize(GObject *obj)
{
	FuLinuxTaintedPlugin *self = FU_LINUX_TAINTED_PLUGIN(obj);
	if (self->file != NULL)
		g_object_unref(self->file);
	if (self->monitor != NULL) {
		g_file_monitor_cancel(self->monitor);
		g_object_unref(self->monitor);
	}
	G_OBJECT_CLASS(fu_linux_tainted_plugin_parent_class)->finalize(obj);
}

static void
fu_linux_tainted_plugin_class_init(FuLinuxTaintedPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_linux_tainted_finalize;
	plugin_class->startup = fu_linux_tainted_plugin_startup;
	plugin_class->add_security_attrs = fu_linux_tainted_plugin_add_security_attrs;
}
