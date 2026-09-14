/*
 * Copyright 2022 Mario Limonciello <mario.limonciello@amd.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-linux-fwattr-plugin.h"
#include "fu-plugin-private.h"

static FuContext *
fu_linux_fwattr_context_new(void)
{
	g_autofree gchar *quirks_dir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();

	/* load quirks so the canonical-ID and flags mappings are available */
	quirks_dir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, quirks_dir);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);

	return g_steal_pointer(&ctx);
}

static FuPlugin *
fu_lenovo_thinklmi_linux_fwattr_plugin_new(FuContext *ctx)
{
	return fu_plugin_new_from_gtype(fu_linux_fwattr_plugin_get_type(), ctx);
}

static void
fu_linux_fwattr_load_lenovo_p620_problems_func(void)
{
	gboolean ret;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuContext) ctx = fu_linux_fwattr_context_new();
	g_autoptr(FuPlugin) plugin = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load BIOS settings from a Lenovo P620 (with thinklmi driver problems) */
	test_dir = g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "lenovo-p620", NULL);
	if (!g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing test data");
		return;
	}
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_linux_fwattr_load_lenovo_p620_func(void)
{
	FuBiosSetting *setting;
	GPtrArray *values;
	const gchar *tmp;
	gboolean ret;
	gboolean pending_reboot = FALSE;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuContext) ctx = fu_linux_fwattr_context_new();
	g_autoptr(FuPlugin) plugin = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) p620_6_3_items = NULL;

	/* load BIOS settings from a Lenovo P620 running 6.3 */
	test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "lenovo-p620-6.3", NULL);
	if (!g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing test data");
		return;
	}

	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	p620_6_3_items = fu_context_get_bios_settings(ctx);
	g_assert_cmpint(p620_6_3_items->len, ==, 5);
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.WindowsUEFIFirmwareUpdate");
	g_assert_nonnull(setting);
	tmp = fu_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "BIOS updates delivered via LVFS or Windows Update");

	/* make sure nothing pending */
	ret = fu_context_get_pending_reboot(ctx, &pending_reboot, &error);
	g_assert_true(ret);
	g_assert_no_error(error);
	g_assert_false(pending_reboot);

	/* check a BIOS setting reads from kernel 6.3 as expected by fwupd */
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.AMDMemoryGuard");
	g_assert_nonnull(setting);
	tmp = fu_bios_setting_get_name(setting);
	g_assert_cmpstr(tmp, ==, "AMDMemoryGuard");
	tmp = fu_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "AMDMemoryGuard");
	tmp = fu_bios_setting_get_current_value(setting);
	g_assert_cmpstr(tmp, ==, "Disable");
	values = fu_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Disable");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Enable");
	}

	/* try to read an BIOS setting known to have ][Status] to make sure we worked
	 * around the thinklmi bug sufficiently
	 */
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.StartupSequence");
	g_assert_nonnull(setting);
	tmp = fu_bios_setting_get_current_value(setting);
	g_assert_cmpstr(tmp, ==, "Primary");
	values = fu_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Primary");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Automatic");
	}

	/* check BIOS settings that should be read only */
	for (guint i = 0; i < p620_6_3_items->len; i++) {
		const gchar *name;
		gboolean ro;

		setting = g_ptr_array_index(p620_6_3_items, i);
		ro = fu_bios_setting_get_read_only(setting);
		tmp = fu_bios_setting_get_current_value(setting);
		name = fu_bios_setting_get_name(setting);
		g_debug("%s: %s", name, tmp);
		if ((g_strcmp0(name, "pending_reboot") == 0) || (g_strrstr(tmp, "[Status") != NULL))
			g_assert_true(ro);
		else
			g_assert_false(ro);
	}
}

/* load BIOS settings from a Lenovo P14s Gen1 */
static void
fu_linux_fwattr_load_lenovo_p14s_gen1_func(void)
{
	gboolean ret;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuContext) ctx = fu_linux_fwattr_context_new();
	g_autoptr(FuPlugin) plugin = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "lenovo-p14s-gen1", NULL);
	if (!g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing test data");
		return;
	}

	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/* load BIOS settings from a Dell XPS 9310 */
static void
fu_linux_fwattr_load_dell_xps_9310_func(void)
{
	FuBiosSetting *setting;
	FwupdBiosSettingKind kind;
	GPtrArray *values;
	const gchar *tmp;
	gboolean ret;
	gint integer;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuContext) ctx = fu_linux_fwattr_context_new();
	g_autoptr(FuPlugin) plugin = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "dell-xps13-9310", NULL);
	if (!g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing test data");
		return;
	}

	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure that we DIDN'T parse reset_bios setting */
	setting = fu_context_get_bios_setting(ctx, FWUPD_BIOS_SETTING_RESET_BIOS);
	g_assert_null(setting);

	/* look at a integer BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.CustomChargeStop");
	g_assert_nonnull(setting);
	kind = fu_bios_setting_get_kind(setting);
	g_assert_cmpint(kind, ==, FWUPD_BIOS_SETTING_KIND_INTEGER);
	integer = fu_bios_setting_get_lower_bound(setting);
	g_assert_cmpint(integer, ==, 55);
	integer = fu_bios_setting_get_upper_bound(setting);
	g_assert_cmpint(integer, ==, 100);
	integer = fu_bios_setting_get_scalar_increment(setting);
	g_assert_cmpint(integer, ==, 1);

	/* look at a string BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.Asset");
	g_assert_nonnull(setting);
	integer = fu_bios_setting_get_lower_bound(setting);
	g_assert_cmpint(integer, ==, 1);
	integer = fu_bios_setting_get_upper_bound(setting);
	g_assert_cmpint(integer, ==, 64);
	tmp = fu_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "Asset Tag");

	/* look at a enumeration BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.BiosRcvrFrmHdd");
	g_assert_nonnull(setting);
	kind = fu_bios_setting_get_kind(setting);
	g_assert_cmpint(kind, ==, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	values = fu_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Disabled");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Enabled");
	}

	/* make sure we defaulted UEFI Secure boot to read only if enabled */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.SecureBoot");
	g_assert_nonnull(setting);
	ret = fu_bios_setting_get_read_only(setting);
	g_assert_true(ret);

	/* the AppStream ID comes from the quirk mapping; the icon and name
	 * come from the abstract table in fu-bios-setting.c */
	tmp = fu_bios_setting_get_appstream_id(setting);
	g_assert_cmpstr(tmp, ==, "org.fwupd.bios.secure-boot");
	tmp = fu_bios_setting_get_icon(setting);
	g_assert_cmpstr(tmp, ==, "application-certificate");
	tmp = fu_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "Secure Boot");

	/* the same setting can be looked up by its AppStream ID */
	g_assert_true(fu_context_get_bios_setting(ctx, "org.fwupd.bios.secure-boot") == setting);

	/* a device-toggle setting also picks up its AppStream ID */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.Camera");
	g_assert_nonnull(setting);
	tmp = fu_bios_setting_get_appstream_id(setting);
	g_assert_cmpstr(tmp, ==, "org.fwupd.bios.camera.enabled");

	/* an esoteric setting has no AppStream ID */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.Asset");
	g_assert_nonnull(setting);
	tmp = fu_bios_setting_get_appstream_id(setting);
	g_assert_null(tmp);
}

/* load BIOS settings from a HP Z2 Mini G1a */
static void
fu_linux_fwattr_load_hp_z2_mini_func(void)
{
	gboolean ret;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuContext) ctx = fu_linux_fwattr_context_new();
	g_autoptr(FuPlugin) plugin = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "hp-z2-mini-g1a", NULL);
	if (!g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing test data");
		return;
	}

	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/* make sure setup still works, and canonical IDs stay unset, when quirks are disabled */
static void
fu_linux_fwattr_no_quirks_func(void)
{
	gboolean ret;
	const gchar *tmp;
	FuBiosSetting *setting;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuBiosSetting) lenovo_setting = fu_bios_setting_new(ctx);
	g_autoptr(FuPlugin) plugin = fu_lenovo_thinklmi_linux_fwattr_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "dell-xps13-9310", NULL);
	if (!g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing test data");
		return;
	}

	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the setting still loads, but has no AppStream ID */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.SecureBoot");
	g_assert_nonnull(setting);
	tmp = fu_bios_setting_get_appstream_id(setting);
	g_assert_null(tmp);

	/* hardware-specific fallback descriptions do not depend on quirks */
	fu_bios_setting_set_id(lenovo_setting, "com.thinklmi.WindowsUEFIFirmwareUpdate");
	tmp = fu_bios_setting_get_description(lenovo_setting);
	g_assert_cmpstr(tmp, ==, "BIOS updates delivered via LVFS or Windows Update");
}

static void
fu_linux_fwattr_context_lifetime_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBiosSetting) setting = NULL;
	guint ctx_refcount = G_OBJECT(ctx)->ref_count;

	setting = fu_bios_setting_new(ctx);
	g_assert_nonnull(setting);
	g_assert_cmpuint(G_OBJECT(ctx)->ref_count, ==, ctx_refcount);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/linux-fwattr/load/lenovo-p620/problems",
			fu_linux_fwattr_load_lenovo_p620_problems_func);
	g_test_add_func("/fwupd/linux-fwattr/load/lenovo-p620",
			fu_linux_fwattr_load_lenovo_p620_func);
	g_test_add_func("/fwupd/linux-fwattr/load/lenovo-p14-gen1",
			fu_linux_fwattr_load_lenovo_p14s_gen1_func);
	g_test_add_func("/fwupd/linux-fwattr/load/dell-xps-9310",
			fu_linux_fwattr_load_dell_xps_9310_func);
	g_test_add_func("/fwupd/linux-fwattr/load/hp-z2-mini",
			fu_linux_fwattr_load_hp_z2_mini_func);
	g_test_add_func("/fwupd/linux-fwattr/no-quirks", fu_linux_fwattr_no_quirks_func);
	g_test_add_func("/fwupd/linux-fwattr/context-lifetime",
			fu_linux_fwattr_context_lifetime_func);
	return g_test_run();
}
