/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

struct FuPluginData {
	GFile *file;
	GFileMonitor *monitor;
};

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
fu_plugin_linux_tainted_init(FuPlugin *plugin)
{
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
}

static void
fu_plugin_linux_tainted_destroy(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	if (data->file != NULL)
		g_object_unref(data->file);
	if (data->monitor != NULL) {
		g_file_monitor_cancel(data->monitor);
		g_object_unref(data->monitor);
	}
}

static void
fu_plugin_linux_tainted_changed_cb(GFileMonitor *monitor,
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
fu_plugin_linux_tainted_startup(FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autofree gchar *fn = NULL;
	g_autofree gchar *procfs = NULL;

	procfs = fu_common_get_path(FU_PATH_KIND_PROCFS);
	fn = g_build_filename(procfs, "sys", "kernel", "tainted", NULL);
	data->file = g_file_new_for_path(fn);
	data->monitor = g_file_monitor(data->file, G_FILE_MONITOR_NONE, NULL, error);
	if (data->monitor == NULL)
		return FALSE;
	g_signal_connect(G_FILE_MONITOR(data->monitor),
			 "changed",
			 G_CALLBACK(fu_plugin_linux_tainted_changed_cb),
			 plugin);
	return TRUE;
}

static void
fu_plugin_linux_tainted_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	gsize bufsz = 0;
	guint64 value;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attrs_append(attrs, attr);

	/* load file */
	if (!g_file_load_contents(data->file, NULL, &buf, &bufsz, NULL, &error_local)) {
		g_autofree gchar *fn = g_file_get_path(data->file);
		g_warning("could not open %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* do not assume NUL terminated */
	str = g_strndup(buf, bufsz);
	value = g_ascii_strtoull(str, NULL, 10);
	if (value == G_MAXUINT64) {
		g_warning("could not parse %s", str);
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
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_linux_tainted_init;
	vfuncs->destroy = fu_plugin_linux_tainted_destroy;
	vfuncs->startup = fu_plugin_linux_tainted_startup;
	vfuncs->add_security_attrs = fu_plugin_linux_tainted_add_security_attrs;
}
