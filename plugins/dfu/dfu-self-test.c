/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib-object.h>
#include <stdlib.h>
#include <string.h>

#include "dfu-common.h"
#include "dfu-device-private.h"
#include "dfu-firmware.h"
#include "dfu-sector-private.h"
#include "dfu-target-private.h"

#include "fu-test.h"

#include "fwupd-error.h"

static gchar *
dfu_test_get_filename (const gchar *filename)
{
	gchar *tmp;
	char full_tmp[PATH_MAX];
	g_autofree gchar *path = NULL;
	path = g_build_filename (TESTDATADIR, filename, NULL);
	tmp = realpath (path, full_tmp);
	if (tmp == NULL)
		return NULL;
	return g_strdup (full_tmp);
}

static void
dfu_enums_func (void)
{
	for (guint i = 0; i < DFU_STATE_LAST; i++)
		g_assert_cmpstr (dfu_state_to_string (i), !=, NULL);
	for (guint i = 0; i < DFU_STATUS_LAST; i++)
		g_assert_cmpstr (dfu_status_to_string (i), !=, NULL);
}

static GBytes *
dfu_self_test_get_bytes_for_file (GFile *file, GError **error)
{
	gchar *contents = NULL;
	gsize length = 0;
	if (!g_file_load_contents (file, NULL, &contents, &length, NULL, error))
		return NULL;
	return g_bytes_new_take (contents, length);
}

static void
dfu_firmware_raw_func (void)
{
	DfuElement *element;
	DfuImage *image_tmp;
	GBytes *no_suffix_contents;
	gchar buf[256];
	gboolean ret;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;

	/* set up some dummy data */
	for (guint i = 0; i < 256; i++)
		buf[i] = (gchar) i;
	fw = g_bytes_new_static (buf, 256);

	/* load a non DFU firmware */
	firmware = dfu_firmware_new ();
	ret = dfu_firmware_parse_data (firmware, fw, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_RAW);
	image_tmp = dfu_firmware_get_image (firmware, 0xfe);
	g_assert (image_tmp == NULL);
	image_tmp = dfu_firmware_get_image (firmware, 0);
	g_assert (image_tmp != NULL);
	g_assert_cmpint (dfu_image_get_size (image_tmp), ==, 256);
	element = dfu_image_get_element (image_tmp, 0);
	g_assert (element != NULL);
	no_suffix_contents = dfu_element_get_contents (element);
	g_assert (no_suffix_contents != NULL);
	g_assert_cmpint (g_bytes_compare (no_suffix_contents, fw), ==, 0);

	/* can we roundtrip without adding data */
	roundtrip = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (roundtrip != NULL);
	ret = fu_common_bytes_compare (roundtrip, fw, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
}

static void
dfu_firmware_dfu_func (void)
{
	gchar buf[256];
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) roundtrip_orig = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	/* set up some dummy data */
	for (guint i = 0; i < 256; i++)
		buf[i] = (gchar) i;
	fw = g_bytes_new_static (buf, 256);

	/* write DFU format */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU);
	dfu_firmware_set_vid (firmware, 0x1234);
	dfu_firmware_set_pid (firmware, 0x5678);
	dfu_firmware_set_release (firmware, 0xfedc);
	image = dfu_image_new ();
	element = dfu_element_new ();
	dfu_element_set_contents (element, fw);
	dfu_image_add_element (image, element);
	dfu_firmware_add_image (firmware, image);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 256);
	data = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (data != NULL);

	/* can we load it again? */
	g_ptr_array_set_size (dfu_firmware_get_images (firmware), 0);
	ret = dfu_firmware_parse_data (firmware, data, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0x1234);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0x5678);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0xfedc);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_DFU);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 256);

	/* load a real firmware */
	filename = dfu_test_get_filename ("kiibohd.dfu.bin");
	g_assert (filename != NULL);
	file = g_file_new_for_path (filename);
	g_ptr_array_set_size (dfu_firmware_get_images (firmware), 0);
	ret = dfu_firmware_parse_file (firmware, file,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0x1c11);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0xb007);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_DFU);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 0x8eB4);

	/* can we roundtrip without losing data */
	roundtrip_orig = dfu_self_test_get_bytes_for_file (file, &error);
	g_assert_no_error (error);
	g_assert (roundtrip_orig != NULL);
	roundtrip = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (roundtrip != NULL);
	ret = fu_common_bytes_compare (roundtrip, roundtrip_orig, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
}

static void
dfu_firmware_dfuse_func (void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GBytes) roundtrip_orig = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	/* load a DeFUse firmware */
	g_setenv ("DFU_SELF_TEST_IMAGE_MEMCPY_NAME", "", FALSE);
	filename = dfu_test_get_filename ("dev_VRBRAIN.dfu");
	g_assert (filename != NULL);
	file = g_file_new_for_path (filename);
	firmware = dfu_firmware_new ();
	ret = dfu_firmware_parse_file (firmware, file,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0x0483);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0x0000);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0x0000);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_DFUSE);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 0x168d5);

	/* can we roundtrip without losing data */
	roundtrip_orig = dfu_self_test_get_bytes_for_file (file, &error);
	g_assert_no_error (error);
	g_assert (roundtrip_orig != NULL);
	roundtrip = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (roundtrip != NULL);

//	g_file_set_contents ("/tmp/1.bin",
//			     g_bytes_get_data (roundtrip, NULL),
//			     g_bytes_get_size (roundtrip), NULL);

	ret = fu_common_bytes_compare (roundtrip, roundtrip_orig, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* use usual image name copying */
	g_unsetenv ("DFU_SELF_TEST_IMAGE_MEMCPY_NAME");
}

static gchar *
dfu_target_sectors_to_string (DfuTarget *target)
{
	GPtrArray *sectors;
	GString *str;

	str = g_string_new ("");
	sectors = dfu_target_get_sectors (target);
	for (guint i = 0; i < sectors->len; i++) {
		DfuSector *sector = g_ptr_array_index (sectors, i);
		g_autofree gchar *tmp = dfu_sector_to_string (sector);
		g_string_append_printf (str, "%s\n", tmp);
	}
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

static void
dfu_target_dfuse_func (void)
{
	gboolean ret;
	gchar *tmp;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(GError) error = NULL;

	/* NULL */
	target = g_object_new (DFU_TYPE_TARGET, NULL);
	ret = dfu_target_parse_sectors (target, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = dfu_target_sectors_to_string (target);
	g_assert_cmpstr (tmp, ==, "");
	g_free (tmp);

	/* no addresses */
	ret = dfu_target_parse_sectors (target, "@Flash3", &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = dfu_target_sectors_to_string (target);
	g_assert_cmpstr (tmp, ==, "");
	g_free (tmp);

	/* one sector, no space */
	ret = dfu_target_parse_sectors (target, "@Internal Flash /0x08000000/2*001Ka", &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = dfu_target_sectors_to_string (target);
	ret = fu_test_compare_lines (tmp,
				      "Zone:0, Sec#:0, Addr:0x08000000, Size:0x0400, Caps:0x1 [R]\n"
				      "Zone:0, Sec#:0, Addr:0x08000400, Size:0x0400, Caps:0x1 [R]",
				      &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (tmp);

	/* multiple sectors */
	ret = dfu_target_parse_sectors (target, "@Flash1   /0x08000000/2*001Ka,4*001Kg", &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = dfu_target_sectors_to_string (target);
	ret = fu_test_compare_lines (tmp,
				      "Zone:0, Sec#:0, Addr:0x08000000, Size:0x0400, Caps:0x1 [R]\n"
				      "Zone:0, Sec#:0, Addr:0x08000400, Size:0x0400, Caps:0x1 [R]\n"
				      "Zone:0, Sec#:1, Addr:0x08000800, Size:0x0400, Caps:0x7 [REW]\n"
				      "Zone:0, Sec#:1, Addr:0x08000c00, Size:0x0400, Caps:0x7 [REW]\n"
				      "Zone:0, Sec#:1, Addr:0x08001000, Size:0x0400, Caps:0x7 [REW]\n"
				      "Zone:0, Sec#:1, Addr:0x08001400, Size:0x0400, Caps:0x7 [REW]",
				      &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (tmp);

	/* non-contiguous */
	ret = dfu_target_parse_sectors (target, "@Flash2 /0xF000/4*100Ba/0xE000/3*8Kg/0x80000/2*24Kg", &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = dfu_target_sectors_to_string (target);
	ret = fu_test_compare_lines (tmp,
				      "Zone:0, Sec#:0, Addr:0x0000f000, Size:0x0064, Caps:0x1 [R]\n"
				      "Zone:0, Sec#:0, Addr:0x0000f064, Size:0x0064, Caps:0x1 [R]\n"
				      "Zone:0, Sec#:0, Addr:0x0000f0c8, Size:0x0064, Caps:0x1 [R]\n"
				      "Zone:0, Sec#:0, Addr:0x0000f12c, Size:0x0064, Caps:0x1 [R]\n"
				      "Zone:1, Sec#:0, Addr:0x0000e000, Size:0x2000, Caps:0x7 [REW]\n"
				      "Zone:1, Sec#:0, Addr:0x00010000, Size:0x2000, Caps:0x7 [REW]\n"
				      "Zone:1, Sec#:0, Addr:0x00012000, Size:0x2000, Caps:0x7 [REW]\n"
				      "Zone:2, Sec#:0, Addr:0x00080000, Size:0x6000, Caps:0x7 [REW]\n"
				      "Zone:2, Sec#:0, Addr:0x00086000, Size:0x6000, Caps:0x7 [REW]",
				      &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (tmp);

	/* invalid */
	ret = dfu_target_parse_sectors (target, "Flash", NULL);
	g_assert (ret);
	ret = dfu_target_parse_sectors (target, "@Internal Flash /0x08000000", NULL);
	g_assert (!ret);
	ret = dfu_target_parse_sectors (target, "@Internal Flash /0x08000000/12*001a", NULL);
	g_assert (!ret);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* log everything */
	g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);

	/* tests go here */
	g_test_add_func ("/dfu/enums", dfu_enums_func);
	g_test_add_func ("/dfu/target(DfuSe}", dfu_target_dfuse_func);
	g_test_add_func ("/dfu/firmware{raw}", dfu_firmware_raw_func);
	g_test_add_func ("/dfu/firmware{dfu}", dfu_firmware_dfu_func);
	g_test_add_func ("/dfu/firmware{dfuse}", dfu_firmware_dfuse_func);
	return g_test_run ();
}

