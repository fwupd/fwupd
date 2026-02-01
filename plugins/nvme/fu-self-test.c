/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-nvme-device.h"

static void
fu_nvme_serial_suffix_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_NVME_DEVICE, "context", ctx, NULL);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_add_instance_str(device, "VEN", "1234");
	fu_device_add_instance_str(device, "DEV", "5678");
	ret = fu_device_set_quirk_kv(device,
				     "NvmeSerialSuffixChars",
				     "8",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_nvme_device_set_serial(FU_NVME_DEVICE(device), "S6B0NL0ABCDEFGH", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check has SNSUFFIX extra ID */
	str = fu_device_to_string(device);
	g_debug("%s", str);
	g_assert_cmpstr(fu_device_get_serial(device), ==, "S6B0NL0ABCDEFGH");
	g_assert_true(fu_device_has_instance_id(device,
						"NVME\\VEN_1234&DEV_5678&SNSUFFIX_ABCDEFGH",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
}

static void
fu_nvme_cns_func(void)
{
	gboolean ret;
	gsize sz;
	g_autofree gchar *data = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuNvmeDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	path = g_test_build_filename(G_TEST_DIST, "tests", "TOSHIBA_THNSN5512GPU7.bin", NULL);

	if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing TOSHIBA_THNSN5512GPU7.bin");
		return;
	}
	ret = g_file_get_contents(path, &data, &sz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dev = fu_nvme_device_new_from_blob(ctx, (guint8 *)data, sz, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dev);
	fu_device_convert_instance_ids(FU_DEVICE(dev));
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(dev)), ==, "THNSN5512GPU7 TOSHIBA");
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(dev)), ==, "410557LA");
	g_assert_cmpstr(fu_device_get_serial(FU_DEVICE(dev)), ==, "37RSDEADBEEF");
	g_assert_cmpstr(fu_device_get_guid_default(FU_DEVICE(dev)),
			==,
			"e1409b09-50cf-5aef-8ad8-760b9022f88d");
}

static void
fu_nvme_cns_all_func(void)
{
	const gchar *fn;
	g_autofree gchar *path = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GDir) dir = NULL;

	/* may or may not exist */
	path = g_test_build_filename(G_TEST_DIST, "tests", "blobs", NULL);
	if (!g_file_test(path, G_FILE_TEST_EXISTS))
		return;
	dir = g_dir_open(path, 0, NULL);
	while ((fn = g_dir_read_name(dir)) != NULL) {
		gsize sz;
		g_autofree gchar *data = NULL;
		g_autofree gchar *filename = NULL;
		g_autoptr(FuNvmeDevice) dev = NULL;
		g_autoptr(GError) error = NULL;

		filename = g_build_filename(path, fn, NULL);
		g_debug("parsing %s... ", filename);
		if (!g_file_get_contents(filename, &data, &sz, &error)) {
			g_debug("failed to load %s: %s", filename, error->message);
			continue;
		}
		dev = fu_nvme_device_new_from_blob(ctx, (guint8 *)data, sz, &error);
		if (dev == NULL) {
			g_debug("failed to load %s: %s", filename, error->message);
			continue;
		}
		g_assert_cmpstr(fu_device_get_name(FU_DEVICE(dev)), !=, NULL);
		g_assert_cmpstr(fu_device_get_version(FU_DEVICE(dev)), !=, NULL);
		g_assert_cmpstr(fu_device_get_serial(FU_DEVICE(dev)), !=, NULL);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/serial-suffix", fu_nvme_serial_suffix_func);
	g_test_add_func("/fwupd/cns", fu_nvme_cns_func);
	g_test_add_func("/fwupd/cns/all", fu_nvme_cns_all_func);
	return g_test_run();
}
