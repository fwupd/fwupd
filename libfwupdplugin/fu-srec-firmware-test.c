/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_srec_firmware_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "srec.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	data_bin = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 11);
}

static void
fu_srec_firmware_tokenization_func(void)
{
	FuSrecFirmwareRecord *rcd;
	GPtrArray *records;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new();
	g_autoptr(GBytes) data_srec = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *buf = "S3060000001400E5\r\n"
			   "S31000000002281102000000007F0304002C\r\n"
			   "S306000000145095\r\n"
			   "S70500000000FA\r\n";
	data_srec = g_bytes_new_static(buf, strlen(buf));
	g_assert_no_error(error);
	g_assert_nonnull(data_srec);
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(data_srec));
	ret = fu_firmware_tokenize(firmware, stream, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	records = fu_srec_firmware_get_records(FU_SREC_FIRMWARE(firmware));
	g_assert_nonnull(records);
	g_assert_cmpint(records->len, ==, 4);
	rcd = g_ptr_array_index(records, 2);
	g_assert_nonnull(rcd);
	g_assert_cmpint(rcd->ln, ==, 0x3);
	g_assert_cmpint(rcd->kind, ==, 3);
	g_assert_cmpint(rcd->addr, ==, 0x14);
	g_assert_cmpint(rcd->buf->len, ==, 0x1);
	g_assert_cmpint(rcd->buf->data[0], ==, 0x50);
}

static void
fu_srec_firmware_addr24_func(void)
{
	FuSrecFirmwareRecord *rcd;
	GPtrArray *records;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new();
	g_autoptr(GBytes) data_srec = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *buf = "S205123456AAB4\r\n"
			   "S804000000FB\r\n";

	/* S2 records have a 24 bit address */
	data_srec = g_bytes_new_static(buf, strlen(buf));
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(data_srec));
	ret = fu_firmware_tokenize(firmware, stream, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	records = fu_srec_firmware_get_records(FU_SREC_FIRMWARE(firmware));
	g_assert_nonnull(records);
	g_assert_cmpint(records->len, ==, 2);
	rcd = g_ptr_array_index(records, 0);
	g_assert_cmpint(rcd->kind, ==, 2);
	g_assert_cmpint(rcd->addr, ==, 0x123456);
	g_assert_cmpint(rcd->buf->len, ==, 0x1);
	g_assert_cmpint(rcd->buf->data[0], ==, 0xAA);
}

static void
fu_srec_firmware_hole_func(void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	const gchar *buf = "S0030000FC\r\n"
			   "S30600000010AA3F\r\n"
			   "S30600000012BB2C\r\n"
			   "S70500000000FA\r\n";
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new();
	g_autoptr(GBytes) blob = g_bytes_new_static(buf, strlen(buf));
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GError) error = NULL;

	/* a one byte hole is padded */
	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fw);
	data = g_bytes_get_data(data_fw, &len);
	g_assert_cmpint(len, ==, 3);
	g_assert_cmpint(data[0], ==, 0xAA);
	g_assert_cmpint(data[1], ==, 0xFF);
	g_assert_cmpint(data[2], ==, 0xBB);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_SREC_FIRMWARE);
	g_test_add_func("/fwupd/srec-firmware", fu_srec_firmware_func);
	g_test_add_func("/fwupd/srec-firmware/tokenization", fu_srec_firmware_tokenization_func);
	g_test_add_func("/fwupd/srec-firmware/addr24", fu_srec_firmware_addr24_func);
	g_test_add_func("/fwupd/srec-firmware/hole", fu_srec_firmware_hole_func);
	return g_test_run();
}
