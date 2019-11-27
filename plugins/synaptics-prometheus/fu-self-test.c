/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-plugin-private.h"

#include "fu-synaprom-device.h"
#include "fu-synaprom-firmware.h"

static void
fu_test_synaprom_firmware_func (void)
{
	const guint8 *buf;
	gboolean ret;
	gsize sz = 0;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuSynapromDevice) device = fu_synaprom_device_new (NULL);
	g_autoptr(GBytes) blob1 = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;
	g_autoptr(FuFirmware) firmware = fu_synaprom_firmware_new ();

	filename = g_build_filename (TESTDATADIR, "test.pkg", NULL);
	fw = fu_common_get_contents_bytes (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fw);
	buf = g_bytes_get_data (fw, &sz);
	g_assert_cmpint (sz, ==, 294);
	g_assert_cmpint (buf[0], ==, 0x01);
	g_assert_cmpint (buf[1], ==, 0x00);
	ret = fu_firmware_parse (firmware, fw, 0, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* does not exist */
	blob1 = fu_firmware_get_image_by_id_bytes (firmware, "NotGoingToExist", NULL);
	g_assert_null (blob1);
	blob1 = fu_firmware_get_image_by_id_bytes (firmware, "cfg-update-header", NULL);
	g_assert_null (blob1);

	/* header needs to exist */
	blob1 = fu_firmware_get_image_by_id_bytes (firmware, "mfw-update-header", &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob1);
	buf = g_bytes_get_data (blob1, &sz);
	g_assert_cmpint (sz, ==, 24);
	g_assert_cmpint (buf[0], ==, 0x41);
	g_assert_cmpint (buf[1], ==, 0x00);
	g_assert_cmpint (buf[2], ==, 0x00);
	g_assert_cmpint (buf[3], ==, 0x00);
	g_assert_cmpint (buf[4], ==, 0xff);

	/* payload needs to exist */
	fu_synaprom_device_set_version (device, 10, 1, 1234);
	firmware2 = fu_synaprom_device_prepare_fw (FU_DEVICE (device), fw,
						   FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (firmware2);
	blob2 = fu_firmware_get_image_by_id_bytes (firmware2, "mfw-update-payload", &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob2);
	buf = g_bytes_get_data (blob2, &sz);
	g_assert_cmpint (sz, ==, 2);
	g_assert_cmpint (buf[0], ==, 'R');
	g_assert_cmpint (buf[1], ==, 'H');
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_func ("/synaprom/firmware", fu_test_synaprom_firmware_func);
	return g_test_run ();
}
