/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-bios-settings-private.h"
#include "fu-context-private.h"

static void
fu_bios_settings_load_func(void)
{
	gboolean ret;
	gint integer;
	const gchar *tmp;
	GPtrArray *values;
	FwupdBiosSetting *setting;
	FwupdBiosSettingKind kind;
	g_autofree gchar *base_dir = NULL;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FuBiosSettings) p620_6_3_settings = NULL;
	g_autoptr(GPtrArray) p620_6_3_items = NULL;

#ifdef _WIN32
	/* BIOS settings are Linux-specific (require sysfs). The test data directory
	 * "AlarmDate(MM\DD\YYYY)" was also causing repository clone failures on Windows,
	 * which has been fixed by renaming it to "AlarmDate-MM-DD-YYYY" */
	g_test_skip("BIOS settings not supported on Windows");
	return;
#endif

	/* ensure the data directory is actually present for the test */
	base_dir = g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", NULL);
	if (!g_file_test(base_dir, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing test data");
		return;
	}

	/* load BIOS settings from a Lenovo P620 (with thinklmi driver problems) */
	test_dir = g_build_filename(base_dir, "lenovo-p620", NULL);
	if (g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
		ret = fu_context_reload_bios_settings(ctx, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}
	g_free(test_dir);

	/* load BIOS settings from a Lenovo P620 running 6.3 */
	test_dir = g_build_filename(base_dir, "lenovo-p620-6.3", NULL);
	if (g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
		ret = fu_context_reload_bios_settings(ctx, &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		p620_6_3_settings = fu_context_get_bios_settings(ctx);
		p620_6_3_items = fu_bios_settings_get_all(p620_6_3_settings);
		g_assert_cmpint(p620_6_3_items->len, ==, 5);

		/* make sure nothing pending */
		ret = fu_context_get_bios_setting_pending_reboot(ctx);
		g_assert_false(ret);

		/* check a BIOS setting reads from kernel 6.3 as expected by fwupd */
		setting = fu_context_get_bios_setting(ctx, "com.thinklmi.AMDMemoryGuard");
		g_assert_nonnull(setting);
		tmp = fwupd_bios_setting_get_name(setting);
		g_assert_cmpstr(tmp, ==, "AMDMemoryGuard");
		tmp = fwupd_bios_setting_get_description(setting);
		g_assert_cmpstr(tmp, ==, "AMDMemoryGuard");
		tmp = fwupd_bios_setting_get_current_value(setting);
		g_assert_cmpstr(tmp, ==, "Disable");
		values = fwupd_bios_setting_get_possible_values(setting);
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
		tmp = fwupd_bios_setting_get_current_value(setting);
		g_assert_cmpstr(tmp, ==, "Primary");
		values = fwupd_bios_setting_get_possible_values(setting);
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
			ro = fwupd_bios_setting_get_read_only(setting);
			tmp = fwupd_bios_setting_get_current_value(setting);
			name = fwupd_bios_setting_get_name(setting);
			g_debug("%s: %s", name, tmp);
			if ((g_strcmp0(name, "pending_reboot") == 0) ||
			    (g_strrstr(tmp, "[Status") != NULL))
				g_assert_true(ro);
			else
				g_assert_false(ro);
		}
	}
	g_free(test_dir);

	/* load BIOS settings from a Lenovo P14s Gen1 */
	test_dir = g_build_filename(base_dir, "lenovo-p14s-gen1", NULL);
	if (g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
		ret = fu_context_reload_bios_settings(ctx, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_clear_error(&error);
	}
	g_free(test_dir);

	/* load BIOS settings from a Dell XPS 9310 */
	test_dir = g_build_filename(base_dir, "dell-xps13-9310", NULL);
	if (g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
		ret = fu_context_reload_bios_settings(ctx, &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		/* make sure that we DIDN'T parse reset_bios setting */
		setting = fu_context_get_bios_setting(ctx, FWUPD_BIOS_SETTING_RESET_BIOS);
		g_assert_null(setting);

		/* look at a integer BIOS setting */
		setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.CustomChargeStop");
		g_assert_nonnull(setting);
		kind = fwupd_bios_setting_get_kind(setting);
		g_assert_cmpint(kind, ==, FWUPD_BIOS_SETTING_KIND_INTEGER);
		integer = fwupd_bios_setting_get_lower_bound(setting);
		g_assert_cmpint(integer, ==, 55);
		integer = fwupd_bios_setting_get_upper_bound(setting);
		g_assert_cmpint(integer, ==, 100);
		integer = fwupd_bios_setting_get_scalar_increment(setting);
		g_assert_cmpint(integer, ==, 1);

		/* look at a string BIOS setting */
		setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.Asset");
		g_assert_nonnull(setting);
		integer = fwupd_bios_setting_get_lower_bound(setting);
		g_assert_cmpint(integer, ==, 1);
		integer = fwupd_bios_setting_get_upper_bound(setting);
		g_assert_cmpint(integer, ==, 64);
		tmp = fwupd_bios_setting_get_description(setting);
		g_assert_cmpstr(tmp, ==, "Asset Tag");

		/* look at a enumeration BIOS setting */
		setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.BiosRcvrFrmHdd");
		g_assert_nonnull(setting);
		kind = fwupd_bios_setting_get_kind(setting);
		g_assert_cmpint(kind, ==, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
		values = fwupd_bios_setting_get_possible_values(setting);
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
		ret = fwupd_bios_setting_get_read_only(setting);
		g_assert_true(ret);
	}
	g_free(test_dir);

	/* load BIOS settings from a HP Z2 Mini G1a */
	test_dir = g_build_filename(base_dir, "hp-z2-mini-g1a", NULL);
	if (g_file_test(test_dir, G_FILE_TEST_EXISTS)) {
		fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, test_dir);
		ret = fu_context_reload_bios_settings(ctx, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/bios-settings/load", fu_bios_settings_load_func);
	return g_test_run();
}
