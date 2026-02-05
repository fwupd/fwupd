/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-smbios-private.h"

static void
fu_smbios_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autofree gchar *full_path = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

#ifdef _WIN32
	g_test_skip("Windows uses GetSystemFirmwareTable rather than parsing the fake test data");
	return;
#endif

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_path_store_set_path(pstore, FU_PATH_KIND_SYSFSDIR_FW, testdatadir);

	full_path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "tables", NULL);
	if (!g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
		g_test_skip("no DMI tables found");
		return;
	}

	smbios = fu_smbios_new(pstore);
	ret = fu_smbios_setup(smbios, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
	g_debug("%s", dump);

	/* test for missing table */
	str = fu_smbios_get_string(smbios, 0xff, FU_SMBIOS_STRUCTURE_LENGTH_ANY, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* check for invalid offset */
	str = fu_smbios_get_string(smbios,
				   FU_SMBIOS_STRUCTURE_TYPE_BIOS,
				   FU_SMBIOS_STRUCTURE_LENGTH_ANY,
				   0xff,
				   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* check for invalid length */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x01, 0xff, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* get vendor -- explicit length */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x18, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "LENOVO");

	/* get vendor */
	str = fu_smbios_get_string(smbios,
				   FU_SMBIOS_STRUCTURE_TYPE_BIOS,
				   FU_SMBIOS_STRUCTURE_LENGTH_ANY,
				   0x04,
				   &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "LENOVO");
}

static void
fu_smbios3_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "tables64", NULL);

	if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
		g_test_skip("no DMI tables found");
		return;
	}

	smbios = fu_smbios_new(pstore);
	ret = fu_smbios_setup_from_path(smbios, path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
	g_debug("%s", dump);

	/* get vendor */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x18, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "Dell Inc.");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/smbios", fu_smbios_func);
	g_test_add_func("/fwupd/smbios3", fu_smbios3_func);
	return g_test_run();
}
