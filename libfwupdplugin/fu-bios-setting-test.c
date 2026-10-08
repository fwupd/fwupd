/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"

static void
fu_bios_setting_appstream_id_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBiosSetting) setting = fu_bios_setting_new(ctx);

	/* known AppStream ID gets a description and icon */
	fu_bios_setting_set_appstream_id(setting, "org.fwupd.bios.secure-boot");
	g_assert_cmpstr(fu_bios_setting_get_appstream_id(setting),
			==,
			"org.fwupd.bios.secure-boot");
	g_assert_cmpstr(fu_bios_setting_get_description(setting), ==, "Secure Boot");
	g_assert_cmpstr(fu_bios_setting_get_icon(setting), ==, "application-certificate");

	/* unknown AppStream ID is still stored, with no fallback description */
	fu_bios_setting_set_appstream_id(setting, "org.fwupd.bios.does-not-exist");
	g_assert_cmpstr(fu_bios_setting_get_appstream_id(setting),
			==,
			"org.fwupd.bios.does-not-exist");
}

static void
fu_bios_setting_name_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBiosSetting) setting = fu_bios_setting_new(ctx);

	/* the magic "pending_reboot" name gets a canned description */
	fu_bios_setting_set_name(setting, "pending_reboot");
	g_assert_cmpstr(fu_bios_setting_get_name(setting), ==, "pending_reboot");
	g_assert_cmpstr(fu_bios_setting_get_description(setting),
			==,
			"Settings will apply after system reboots");

	/* a normal name has no fixups */
	fu_bios_setting_set_name(setting, "some_random_name");
	g_assert_cmpstr(fu_bios_setting_get_name(setting), ==, "some_random_name");
}

static void
fu_bios_setting_id_func(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuBiosSetting) setting = fu_bios_setting_new(ctx);
	g_autoptr(GError) error = NULL;

	/* fu_bios_setting_set_id() looks up quirks, so the context must be loaded */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the thinklmi fallback gets a canned description */
	fu_bios_setting_set_id(setting, "com.thinklmi.WindowsUEFIFirmwareUpdate");
	g_assert_cmpstr(fu_bios_setting_get_id(setting),
			==,
			"com.thinklmi.WindowsUEFIFirmwareUpdate");
	g_assert_cmpstr(fu_bios_setting_get_description(setting),
			==,
			"BIOS updates delivered via LVFS or Windows Update");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/bios-setting/appstream-id", fu_bios_setting_appstream_id_func);
	g_test_add_func("/fwupd/bios-setting/name", fu_bios_setting_name_func);
	g_test_add_func("/fwupd/bios-setting/id", fu_bios_setting_id_func);
	return g_test_run();
}
