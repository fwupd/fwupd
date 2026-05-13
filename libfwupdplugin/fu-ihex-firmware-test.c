/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_ihex_firmware_func(void)
{
	const guint8 *data;
	gsize len;
	g_autofree gchar *filename_hex = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GBytes) data_hex = NULL;
	g_autoptr(GError) error = NULL;

	/* load a Intel hex32 file */
	filename_hex = g_test_build_filename(G_TEST_DIST, "tests", "ihex.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename_hex, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	data_fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fw);
	g_assert_cmpint(g_bytes_get_size(data_fw), ==, 92);

	/* export a ihex file (which will be slightly different due to
	 * non-continuous regions being expanded */
	data_hex = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_hex);
	data = g_bytes_get_data(data_hex, &len);
	str = g_strndup((const gchar *)data, len);
	g_assert_cmpstr(str,
			==,
			":100000004E6571756520706F72726F2071756973BE\n"
			":100010007175616D206573742071756920646F6CF2\n"
			":100020006F72656D20697073756D207175696120DF\n"
			":10003000646F6C6F722073697420616D65742C201D\n"
			":10004000636F6E73656374657475722C2061646987\n"
			":0C00500070697363692076656C69740A3E\n"
			":040000FD646176655F\n"
			":00000001FF\n");
}

static void
fu_ihex_firmware_signed_func(void)
{
	const guint8 *data;
	gsize len;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;

	/* load a signed Intel hex32 file */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "ihex-signed.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	data_fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fw);
	g_assert_cmpint(g_bytes_get_size(data_fw), ==, 11);

	/* get the signed image */
	data_sig = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_SIGNATURE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_sig);
	data = g_bytes_get_data(data_sig, &len);
	g_assert_cmpint(len, ==, 8);
	g_assert_nonnull(data);
	g_assert_cmpint(memcmp(data, "deadbeef", 8), ==, 0);
}

static void
fu_ihex_firmware_offset_func(void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new();
	g_autoptr(FuFirmware) firmware_verify = fu_ihex_firmware_new();
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_dummy = NULL;
	g_autoptr(GBytes) data_verify = NULL;
	g_autoptr(GError) error = NULL;

	/* add a 4 byte image in high memory */
	data_dummy = g_bytes_new_static("foo", 4);
	fu_firmware_set_addr(firmware, 0x80000000);
	fu_firmware_set_bytes(firmware, data_dummy);
	data_bin = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	data = g_bytes_get_data(data_bin, &len);
	str = g_strndup((const gchar *)data, len);
	g_assert_cmpstr(str,
			==,
			":0200000480007A\n"
			":04000000666F6F00B8\n"
			":00000001FF\n");

	/* check we can load it too */
	ret = fu_firmware_parse_bytes(firmware_verify,
				      data_bin,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_firmware_get_addr(firmware_verify), ==, 0x80000000);
	data_verify = fu_firmware_get_bytes(firmware_verify, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_verify);
	g_assert_cmpint(g_bytes_get_size(data_verify), ==, 0x4);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_IHEX_FIRMWARE);
	g_test_add_func("/fwupd/ihex-firmware", fu_ihex_firmware_func);
	g_test_add_func("/fwupd/ihex-firmware/offset", fu_ihex_firmware_offset_func);
	g_test_add_func("/fwupd/ihex-firmware/signed", fu_ihex_firmware_signed_func);
	return g_test_run();
}
