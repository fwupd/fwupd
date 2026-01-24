/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-config-private.h"
#include "fu-engine-config.h"

static void
fu_test_copy_file(const gchar *source, const gchar *target)
{
	gboolean ret;
	g_autofree gchar *data = NULL;
	g_autoptr(GError) error = NULL;

	g_debug("copying %s to %s", source, target);
	ret = g_file_get_contents(source, &data, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(target, data, -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_config_migrate_1_9_func(void)
{
	gboolean ret;
	g_autofree gchar *fake_localconf_fn = NULL;
	g_autofree gchar *fake_sysconf_fn = NULL;
	g_autoptr(FuConfig) config = FU_CONFIG(fu_engine_config_new());
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("config-migrate", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	(void)g_setenv("FWUPD_SYSCONFDIR", fu_temporary_directory_get_path(tmpdir), TRUE);
	fake_sysconf_fn = fu_temporary_directory_build(tmpdir, "fwupd", "fwupd.conf", NULL);
	ret = fu_path_mkdir_parent(fake_sysconf_fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(fake_sysconf_fn,
				  "# use `man 5 fwupd.conf` for documentation\n"
				  "[fwupd]\n"
				  "DisabledPlugins=test;test_ble\n"
				  "OnlyTrusted=true\n"
				  "AllowEmulation=false\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_load(config, FU_CONFIG_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* ensure that all keys migrated */
	fake_localconf_fn =
	    fu_temporary_directory_build(tmpdir, "var", "etc", "fwupd", "fwupd.conf", NULL);
	ret = g_file_test(fake_localconf_fn, G_FILE_TEST_EXISTS);
	g_assert_false(ret);
}

static void
fu_config_set_plugin_defaults(FuConfig *config)
{
	/* these are correct for v2.0.0 */
	fu_config_set_default(config, "msr", "MinimumSmeKernelVersion", "5.18.0");
	fu_config_set_default(config, "redfish", "CACheck", "false");
	fu_config_set_default(config, "redfish", "IpmiDisableCreateUser", "false");
	fu_config_set_default(config, "redfish", "ManagerResetTimeout", "1800"); /* seconds */
	fu_config_set_default(config, "redfish", "Password", NULL);
	fu_config_set_default(config, "redfish", "Uri", NULL);
	fu_config_set_default(config, "redfish", "Username", NULL);
	fu_config_set_default(config, "redfish", "UserUri", NULL);
	fu_config_set_default(config, "thunderbolt", "DelayedActivation", "false");
	fu_config_set_default(config, "thunderbolt", "MinimumKernelVersion", "4.13.0");
	fu_config_set_default(config, "uefi-capsule", "DisableCapsuleUpdateOnDisk", "false");
	fu_config_set_default(config, "uefi-capsule", "DisableShimForSecureBoot", "false");
	fu_config_set_default(config, "uefi-capsule", "EnableEfiDebugging", "false");
	fu_config_set_default(config, "uefi-capsule", "EnableGrubChainLoad", "false");
	fu_config_set_default(config, "uefi-capsule", "OverrideESPMountPoint", NULL);
	fu_config_set_default(config, "uefi-capsule", "RebootCleanup", "true");
	fu_config_set_default(config, "uefi-capsule", "RequireESPFreeSpace", "0");
	fu_config_set_default(config, "uefi-capsule", "ScreenWidth", "0");
	fu_config_set_default(config, "uefi-capsule", "ScreenHeight", "0");
}

static void
fu_config_migrate_1_7_func(void)
{
	gboolean ret;
	const gchar *fn_merge[] = {
	    "daemon.conf",
	    "msr.conf",
	    "redfish.conf",
	    "thunderbolt.conf",
	    "uefi_capsule.conf",
	};
	g_autofree gchar *localconf_data = NULL;
	g_autofree gchar *fn_mut = NULL;
	g_autofree gchar *sysconfdir = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuConfig) config = FU_CONFIG(fu_engine_config_new());
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("config-migrate", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	/* source directory and data */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "conf-migration-1.7", NULL);
	if (!g_file_test(testdatadir, G_FILE_TEST_EXISTS)) {
		g_test_skip("missing fwupd 1.7.x migration test data");
		return;
	}

	/* working directory */
	sysconfdir = fu_temporary_directory_build(tmpdir, "etc", NULL);
	localstatedir = fu_temporary_directory_build(tmpdir, "var", NULL);
	(void)g_setenv("FWUPD_SYSCONFDIR", sysconfdir, TRUE);
	(void)g_setenv("FWUPD_LOCALSTATEDIR", localstatedir, TRUE);

	fn_mut = g_build_filename(sysconfdir, "fwupd", "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* copy all files to working directory */
	for (guint i = 0; i < G_N_ELEMENTS(fn_merge); i++) {
		g_autofree gchar *source =
		    g_build_filename(testdatadir, "fwupd", fn_merge[i], NULL);
		g_autofree gchar *target = g_build_filename(sysconfdir, "fwupd", fn_merge[i], NULL);
		fu_test_copy_file(source, target);
	}

	/* we don't want to run all the plugins just to get the _init() defaults */
	fu_config_set_plugin_defaults(config);
	ret = fu_config_load(config, FU_CONFIG_LOAD_FLAG_MIGRATE_FILES, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure all migrated files were renamed */
	for (guint i = 0; i < G_N_ELEMENTS(fn_merge); i++) {
		g_autofree gchar *old = g_build_filename(sysconfdir, "fwupd", fn_merge[i], NULL);
		g_autofree gchar *new = g_strdup_printf("%s.old", old);
		ret = g_file_test(old, G_FILE_TEST_EXISTS);
		g_assert_false(ret);
		ret = g_file_test(new, G_FILE_TEST_EXISTS);
		g_assert_true(ret);
	}

	/* ensure all default keys migrated */
	ret = g_file_get_contents(fn_mut, &localconf_data, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(localconf_data, ==, "");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/engine/config/migrate_1_7", fu_config_migrate_1_7_func);
	g_test_add_func("/fwupd/engine/config/migrate_1_9", fu_config_migrate_1_9_func);
	return g_test_run();
}
