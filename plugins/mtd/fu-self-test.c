/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-mtd-device.h"
#include "fu-udev-device-private.h"

static void
fu_test_mtd_device_func(void)
{
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
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GRand) rand = g_rand_new_with_seed(0);

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_SMBIOS, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create device */
	device = g_object_new(FU_TYPE_MTD_DEVICE,
			      "context",
			      ctx,
			      "backend-id",
			      "/sys/devices/virtual/mtd/mtd0",
			      NULL);
	locker = fu_device_locker_new(device, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
	    g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no permission to read mtdram device");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(locker);
	if (!g_file_test(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)),
			 G_FILE_TEST_EXISTS)) {
		g_test_skip("/dev/mtd0 doesn't exist");
		return;
	}
	if (g_strcmp0(fu_device_get_name(device), "mtdram test device") != 0) {
		g_test_skip("device is not mtdram test device");
		return;
	}

	bufsz = fu_device_get_firmware_size_max(device);
	g_assert_cmpint(bufsz, ==, 0x400000);

	/* create a random payload exactly the same size */
	for (gsize i = 0; i < bufsz; i++)
		fu_byte_array_append_uint8(buf, g_rand_int_range(rand, 0x00, 0xFF));
	fw = g_bytes_new(buf->data, buf->len);

	/* write with a verify */
	stream = g_memory_input_stream_new_from_bytes(fw);
	ret = fu_device_write_firmware(device, stream, progress, FWUPD_INSTALL_FLAG_NONE, &error);
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
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("FWUPD_MTD_VERBOSE", "1", TRUE);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("CONFIGURATION_DIRECTORY", testdatadir, TRUE);

	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_func("/mtd/device", fu_test_mtd_device_func);
	return g_test_run();
}
