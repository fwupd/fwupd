/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ata-device.h"
#include "fu-context-private.h"
#include "fu-device-private.h"

static void
fu_ata_id_func(void)
{
	gboolean ret;
	gsize sz;
	const gchar *ci = g_getenv("CI_NETWORK");
	g_autofree gchar *data = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuAtaDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	path = g_test_build_filename(G_TEST_DIST, "tests", "StarDrive-SBFM61.2.bin", NULL);
	if (!g_file_test(path, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing StarDrive-SBFM61.2.bin");
		return;
	}
	ret = g_file_get_contents(path, &data, &sz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev = fu_ata_device_new_from_blob(ctx, (guint8 *)data, sz, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dev);
	g_assert_cmpint(fu_ata_device_get_transfer_mode(dev), ==, 0xe);
	g_assert_cmpint(fu_ata_device_get_transfer_blocks(dev), ==, 0x1);
	g_assert_cmpstr(fu_device_get_serial(FU_DEVICE(dev)), ==, "A45A078A198600476509");
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(dev)), ==, "SATA SSD");
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(dev)), ==, "SBFM61.2");
}

static void
fu_ata_oui_func(void)
{
	gboolean ret;
	gsize sz;
	const gchar *ci = g_getenv("CI_NETWORK");
	g_autofree gchar *data = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuAtaDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	path = g_test_build_filename(G_TEST_DIST, "tests", "Samsung SSD 860 EVO 500GB.bin", NULL);
	if (!g_file_test(path, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing Samsung SSD 860 EVO 500GB.bin");
		return;
	}
	ret = g_file_get_contents(path, &data, &sz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev = fu_ata_device_new_from_blob(ctx, (guint8 *)data, sz, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dev);
	fu_device_convert_instance_ids(FU_DEVICE(dev));
	str = fu_device_to_string(FU_DEVICE(dev));
	g_debug("%s", str);
	g_assert_cmpint(fu_ata_device_get_transfer_mode(dev), ==, 0xe);
	g_assert_cmpint(fu_ata_device_get_transfer_blocks(dev), ==, 0x1);
	g_assert_cmpstr(fu_device_get_serial(FU_DEVICE(dev)), ==, "S3Z1NB0K862928X");
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(dev)), ==, "SSD 860 EVO 500GB");
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(dev)), ==, "RVT01B6Q");
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);

	/* tests go here */
	g_test_add_func("/fwupd/ata/id", fu_ata_id_func);
	g_test_add_func("/fwupd/ata/oui", fu_ata_oui_func);
	return g_test_run();
}
