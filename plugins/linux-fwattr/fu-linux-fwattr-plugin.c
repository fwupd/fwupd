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
};

G_DEFINE_TYPE(FuLinuxFwattrPlugin, fu_linux_fwattr_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_linux_fwattr_plugin_startup_driver(FuLinuxFwattrPlugin *self,
				      const gchar *driver,
				      const gchar *path,
				      GError **error)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
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
		full_path = g_build_filename(path, name, NULL);
		setting = fu_linux_fwattr_setting_new(ctx, driver, full_path, name, &error_local);
		if (setting == NULL) {
			g_debug("%s is not supported: %s", name, error_local->message);
			continue;
		}
		if (!fu_context_add_bios_setting(ctx, FU_BIOS_SETTING(setting), error))
			return FALSE;
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
}

static void
fu_linux_fwattr_plugin_class_init(FuLinuxFwattrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_linux_fwattr_plugin_startup;
}
