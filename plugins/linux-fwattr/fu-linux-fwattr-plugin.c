/*
 * Copyright 2022 Mario Limonciello <mario.limonciello@amd.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-linux-fwattr-plugin.h"
#include "fu-linux-fwattr-setting.h"

struct _FuLinuxFwattrPlugin {
	FuPlugin parent_instance;
	GPtrArray *monitors; /* element-type GFileMonitor */
};

G_DEFINE_TYPE(FuLinuxFwattrPlugin, fu_linux_fwattr_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_linux_fwattr_plugin_ensure_pending_reboot(FuLinuxFwattrPlugin *self,
					     const gchar *fn,
					     GError **error)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	guint64 val = 0;
	g_autofree gchar *tmp = NULL;

	/* refresh/re-read */
	if (!g_file_get_contents(fn, &tmp, NULL, error)) {
		g_prefix_error_literal(error, "failed to load pending_reboot: ");
		fwupd_error_convert(error);
		return FALSE;
	}
	if (!fu_strtoull(tmp, &val, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	if (val == 1)
		fu_context_add_flag(ctx, FU_CONTEXT_FLAG_PENDING_REBOOT);

	/* success */
	return TRUE;
}

static void
fu_linux_fwattr_plugin_monitor_changed_cb(GFileMonitor *monitor,
					  GFile *file,
					  GFile *other_file,
					  GFileMonitorEvent event_type,
					  FuLinuxFwattrPlugin *self)
{
	g_autofree gchar *fn = g_file_get_path(file);
	g_autoptr(GError) error_local = NULL;
	if (!fu_linux_fwattr_plugin_ensure_pending_reboot(self, fn, &error_local))
		g_warning("failed to rescan %s when changed: %s", fn, error_local->message);
}

static gboolean
fu_linux_fwattr_plugin_startup_driver(FuLinuxFwattrPlugin *self,
				      const gchar *driver,
				      const gchar *path,
				      GError **error)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	g_autofree gchar *pending_fn = NULL;
	g_autoptr(GDir) dir = NULL;

	dir = g_dir_open(path, 0, error);
	if (dir == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	while (TRUE) {
		const gchar *name = g_dir_read_name(dir);
		g_autofree gchar *full_path = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FuLinuxFwattrSetting) setting = NULL;

		if (name == NULL)
			break;

		/* this is handled as a context flag */
		if (g_strcmp0(name, "pending_reboot") == 0)
			continue;

		full_path = g_build_filename(path, name, NULL);
		setting = fu_linux_fwattr_setting_new(ctx, driver, full_path, name, &error_local);
		if (setting == NULL) {
			g_debug("%s is not supported: %s", name, error_local->message);
			continue;
		}
		if (!fu_context_add_bios_setting(ctx, FU_BIOS_SETTING(setting), error))
			return FALSE;
	}

	/* watch in case fwupd (or anything else) sets a BIOS setting */
	pending_fn = g_build_filename(path, "pending_reboot", NULL);
	if (g_file_test(pending_fn, G_FILE_TEST_EXISTS)) {
		g_autoptr(GFile) pending_file = g_file_new_for_path(pending_fn);
		g_autoptr(GFileMonitor) monitor = NULL;

		/* need reboot already? */
		if (!fu_linux_fwattr_plugin_ensure_pending_reboot(self, pending_fn, error))
			return FALSE;
		monitor = g_file_monitor(pending_file, G_FILE_MONITOR_NONE, NULL, error);
		if (monitor == NULL)
			return FALSE;
		g_signal_connect(monitor,
				 "changed",
				 G_CALLBACK(fu_linux_fwattr_plugin_monitor_changed_cb),
				 self);
		g_ptr_array_add(self->monitors, g_steal_pointer(&monitor));
	}

	/* success */
	return TRUE;
}

static void
fu_linux_fwattr_plugin_combination_fixups(FuLinuxFwattrPlugin *self)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	FuBiosSetting *thinklmi_sb = fu_context_get_bios_setting(ctx, "com.thinklmi.SecureBoot");
	FuBiosSetting *thinklmi_3rd =
	    fu_context_get_bios_setting(ctx, "com.thinklmi.Allow3rdPartyUEFICA");

	if (thinklmi_sb != NULL && thinklmi_3rd != NULL) {
		const gchar *val = fu_bios_setting_get_current_value(thinklmi_3rd);
		if (g_strcmp0(val, "Disable") == 0) {
			g_info("Disabling changing %s since %s is %s",
			       fu_bios_setting_get_name(thinklmi_sb),
			       fu_bios_setting_get_name(thinklmi_3rd),
			       val);
			fu_bios_setting_set_read_only(thinklmi_sb, TRUE);
		}
	}
}

static gboolean
fu_linux_fwattr_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuLinuxFwattrPlugin *self = FU_LINUX_FWATTR_PLUGIN(plugin);
	const gchar *sysfsfwdir = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error_local = NULL;

	sysfsfwdir = fu_context_get_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, error);
	if (sysfsfwdir == NULL)
		return FALSE;
	dir = g_dir_open(sysfsfwdir, 0, error);
	if (dir == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	while (TRUE) {
		g_autofree gchar *path = NULL;
		const gchar *driver = g_dir_read_name(dir);
		if (driver == NULL)
			break;
		path = g_build_filename(sysfsfwdir, driver, "attributes", NULL);
		if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
			g_debug("skipping non-directory %s", path);
			continue;
		}
		if (!fu_linux_fwattr_plugin_startup_driver(self, driver, path, error))
			return FALSE;
	}

	/* fix up some special combinations */
	fu_linux_fwattr_plugin_combination_fixups(self);

	/* success */
	return TRUE;
}

static void
fu_linux_fwattr_plugin_init(FuLinuxFwattrPlugin *self)
{
	self->monitors = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_linux_fwattr_plugin_finalize(GObject *obj)
{
	FuLinuxFwattrPlugin *self = FU_LINUX_FWATTR_PLUGIN(obj);
	g_ptr_array_unref(self->monitors);
	G_OBJECT_CLASS(fu_linux_fwattr_plugin_parent_class)->finalize(obj);
}

static void
fu_linux_fwattr_plugin_class_init(FuLinuxFwattrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_linux_fwattr_plugin_finalize;
	plugin_class->startup = fu_linux_fwattr_plugin_startup;
}
