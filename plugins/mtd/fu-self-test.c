/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-mtd-device.h"

static void
fu_test_mtd_device_func(void)
{
#ifdef HAVE_GUDEV
	gsize bufsz;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw2 = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GRand) rand = g_rand_new_with_seed(0);
	g_autoptr(GUdevClient) udev_client = g_udev_client_new(NULL);
	g_autoptr(GUdevDevice) udev_device = NULL;
	const gchar *dev_name;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	udev_device =
	    g_udev_client_query_by_sysfs_path(udev_client, "/sys/devices/virtual/mtd/mtd0");
	if (udev_device == NULL) {
		g_test_skip("could not find mtdram device");
		return;
	}

	/* create device */
	device = g_object_new(FU_TYPE_MTD_DEVICE, "context", ctx, "udev-device", udev_device, NULL);
	locker = fu_device_locker_new(device, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no permission to read mtdram device");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(locker);

	dev_name = fu_device_get_name(device);
	if (g_strcmp0(dev_name, "mtdram test device") != 0) {
		g_test_skip("device is not mtdram test device");
		return;
	}

	bufsz = fu_device_get_firmware_size_max(device);
	g_assert_cmpint(bufsz, ==, 0x400000);

	/* create a random payload exactly the same size */
	for (gsize i = 0; i < bufsz; i++)
		fu_byte_array_append_uint8(buf, g_rand_int_range(rand, 0x00, 0xFF));
	fw = g_byte_array_free_to_bytes(g_steal_pointer(&buf));

	/* write with a verify */
	ret = fu_device_write_firmware(device, fw, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* dump back */
	fu_progress_reset(progress);
	fw2 = fu_device_dump_firmware(device, progress, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw2);

	/* verify */
	ret = fu_bytes_compare(fw, fw2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
#else
	g_test_skip("no GUdev support");
#endif
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("FWUPD_MTD_VERBOSE", "1", TRUE);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);

	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_func("/mtd/device", fu_test_mtd_device_func);
	return g_test_run();
}
