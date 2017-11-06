/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib-object.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

#include "dfu-chunked.h"
#include "dfu-cipher-xtea.h"
#include "dfu-common.h"
#include "dfu-context.h"
#include "dfu-device-private.h"
#include "dfu-firmware.h"
#include "dfu-patch.h"
#include "dfu-sector-private.h"
#include "dfu-target-private.h"

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

static gboolean
dfu_test_compare_lines (const gchar *txt1, const gchar *txt2, GError **error)
{
	g_autofree gchar *output = NULL;

	/* exactly the same */
	if (g_strcmp0 (txt1, txt2) == 0)
		return TRUE;

	/* matches a pattern */
	if (fnmatch (txt2, txt1, FNM_NOESCAPE) == 0)
		return TRUE;

	/* save temp files and diff them */
	if (!g_file_set_contents ("/tmp/a", txt1, -1, error))
		return FALSE;
	if (!g_file_set_contents ("/tmp/b", txt2, -1, error))
		return FALSE;
	if (!g_spawn_command_line_sync ("diff -urNp /tmp/b /tmp/a",
					&output, NULL, NULL, error))
		return FALSE;

	/* just output the diff */
	g_set_error_literal (error, 1, 0, output);
	return FALSE;
}

static gchar *
_g_bytes_compare_verbose (GBytes *bytes1, GBytes *bytes2)
{
	const guint8 *data1;
	const guint8 *data2;
	gsize length1;
	gsize length2;

	data1 = g_bytes_get_data (bytes1, &length1);
	data2 = g_bytes_get_data (bytes2, &length2);

	/* not the same length */
	if (length1 != length2) {
		return g_strdup_printf ("got %" G_GSIZE_FORMAT " bytes, "
					"expected %" G_GSIZE_FORMAT,
					length1, length2);
	}

	/* return 00 01 02 03 */
	for (guint i = 0; i < length1; i++) {
		if (data1[i] != data2[i]) {
			return g_strdup_printf ("got 0x%02x, expected 0x%02x @ 0x%04x",
						data1[i], data2[i], i);
		}
	}
	return NULL;
}

static void
dfu_cipher_xtea_func (void)
{
	gboolean ret;
	guint8 buf[] = { 'H', 'i', 'y', 'a', 'D', 'a', 'v', 'e' };
	g_autoptr(GError) error = NULL;

	ret = dfu_cipher_encrypt_xtea ("test", buf, sizeof(buf), &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpint (buf[0], ==, 128);
	g_assert_cmpint (buf[1], ==, 220);
	g_assert_cmpint (buf[2], ==, 23);
	g_assert_cmpint (buf[3], ==, 55);
	g_assert_cmpint (buf[4], ==, 201);
	g_assert_cmpint (buf[5], ==, 207);
	g_assert_cmpint (buf[6], ==, 182);
	g_assert_cmpint (buf[7], ==, 177);

	ret = dfu_cipher_decrypt_xtea ("test", buf, sizeof(buf), &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpint (buf[0], ==, 'H');
	g_assert_cmpint (buf[1], ==, 'i');
	g_assert_cmpint (buf[2], ==, 'y');
	g_assert_cmpint (buf[3], ==, 'a');
	g_assert_cmpint (buf[4], ==, 'D');
	g_assert_cmpint (buf[5], ==, 'a');
	g_assert_cmpint (buf[6], ==, 'v');
	g_assert_cmpint (buf[7], ==, 'e');
}

static void
dfu_firmware_xdfu_func (void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	fn = dfu_test_get_filename ("example.xdfu");
	g_assert (fn != NULL);
	firmware = dfu_firmware_new ();
	file = g_file_new_for_path (fn);
	ret = dfu_firmware_parse_file (firmware, file,
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_cipher_kind (firmware), ==, DFU_CIPHER_KIND_XTEA);
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
	ret = dfu_firmware_parse_data (firmware, fw, DFU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_RAW);
	g_assert_cmpint (dfu_firmware_get_cipher_kind (firmware), ==, DFU_CIPHER_KIND_NONE);
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
	g_assert_cmpstr (_g_bytes_compare_verbose (roundtrip, fw), ==, NULL);
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
	ret = dfu_firmware_parse_data (firmware, data, DFU_FIRMWARE_PARSE_FLAG_NONE, &error);
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
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0x1c11);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0xb007);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_DFU);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 0x8eB4);
	g_assert_cmpint (dfu_firmware_get_cipher_kind (firmware), ==, DFU_CIPHER_KIND_NONE);

	/* can we roundtrip without loosing data */
	roundtrip_orig = dfu_self_test_get_bytes_for_file (file, &error);
	g_assert_no_error (error);
	g_assert (roundtrip_orig != NULL);
	roundtrip = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (roundtrip != NULL);
	g_assert_cmpstr (_g_bytes_compare_verbose (roundtrip, roundtrip_orig), ==, NULL);
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
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0x0483);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0x0000);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0x0000);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_DFUSE);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 0x168d5);
	g_assert_cmpint (dfu_firmware_get_cipher_kind (firmware), ==, DFU_CIPHER_KIND_NONE);

	/* can we roundtrip without loosing data */
	roundtrip_orig = dfu_self_test_get_bytes_for_file (file, &error);
	g_assert_no_error (error);
	g_assert (roundtrip_orig != NULL);
	roundtrip = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (roundtrip != NULL);

//	g_file_set_contents ("/tmp/1.bin",
//			     g_bytes_get_data (roundtrip, NULL),
//			     g_bytes_get_size (roundtrip), NULL);

	g_assert_cmpstr (_g_bytes_compare_verbose (roundtrip, roundtrip_orig), ==, NULL);

	/* use usual image name copying */
	g_unsetenv ("DFU_SELF_TEST_IMAGE_MEMCPY_NAME");
}

static void
dfu_firmware_metadata_func (void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GBytes) roundtrip_orig = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	/* load a DFU firmware with a metadata table */
	filename = dfu_test_get_filename ("metadata.dfu");
	g_assert (filename != NULL);
	file = g_file_new_for_path (filename);
	firmware = dfu_firmware_new ();
	ret = dfu_firmware_parse_file (firmware, file,
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 6);
	g_assert_cmpstr (dfu_firmware_get_metadata (firmware, "key"), ==, "value");
	g_assert_cmpstr (dfu_firmware_get_metadata (firmware, "???"), ==, NULL);

	/* can we roundtrip without loosing data */
	roundtrip_orig = dfu_self_test_get_bytes_for_file (file, &error);
	g_assert_no_error (error);
	g_assert (roundtrip_orig != NULL);
	roundtrip = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (roundtrip != NULL);

	g_assert_cmpstr (_g_bytes_compare_verbose (roundtrip, roundtrip_orig), ==, NULL);
}

static void
dfu_firmware_intel_hex_offset_func (void)
{
	DfuElement *element_verify;
	DfuImage *image_verify;
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *str = NULL;
	g_autoptr(DfuElement) element = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuFirmware) firmware_verify = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_dummy = NULL;
	g_autoptr(GError) error = NULL;

	/* add a 4 byte image in high memory */
	element = dfu_element_new ();
	data_dummy = g_bytes_new_static ("foo", 4);
	dfu_element_set_address (element, 0x80000000);
	dfu_element_set_contents (element, data_dummy);
	image = dfu_image_new ();
	dfu_image_add_element (image, element);
	firmware = dfu_firmware_new ();
	dfu_firmware_add_image (firmware, image);
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_INTEL_HEX);
	data_bin = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (data_bin != NULL);
	data = g_bytes_get_data (data_bin, &len);
	str = g_strndup ((const gchar *) data, len);
	g_assert_cmpstr (str, ==,
			 ":0200000480007A\n"
			 ":04000000666F6F00B8\n"
			 ":00000001FF\n");

	/* check we can load it too */
	firmware_verify = dfu_firmware_new ();
	ret = dfu_firmware_parse_data (firmware_verify, data_bin, DFU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	image_verify = dfu_firmware_get_image_default (firmware_verify);
	g_assert (image_verify != NULL);
	element_verify = dfu_image_get_element_default (image);
	g_assert (element_verify != NULL);
	g_assert_cmpint (dfu_element_get_address (element_verify), ==, 0x80000000);
	g_assert_cmpint (g_bytes_get_size (dfu_element_get_contents (element_verify)), ==, 0x4);
}

static void
dfu_firmware_intel_hex_func (void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *filename_hex = NULL;
	g_autofree gchar *filename_ref = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GBytes) data_bin2 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_hex = NULL;
	g_autoptr(GBytes) data_ref = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_bin = NULL;
	g_autoptr(GFile) file_hex = NULL;

	/* load a Intel hex32 file */
	filename_hex = dfu_test_get_filename ("firmware.hex");
	g_assert (filename_hex != NULL);
	file_hex = g_file_new_for_path (filename_hex);
	firmware = dfu_firmware_new ();
	ret = dfu_firmware_parse_file (firmware, file_hex,
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 136);
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_RAW);
	data_bin = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (data_bin != NULL);

	/* did we match the reference file? */
	filename_ref = dfu_test_get_filename ("firmware.bin");
	g_assert (filename_ref != NULL);
	file_bin = g_file_new_for_path (filename_ref);
	data_ref = dfu_self_test_get_bytes_for_file (file_bin, &error);
	g_assert_no_error (error);
	g_assert (data_ref != NULL);
	g_assert_cmpstr (_g_bytes_compare_verbose (data_bin, data_ref), ==, NULL);

	/* export a ihex file (which will be slightly different due to
	 * non-continous regions being expanded */
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_INTEL_HEX);
	data_hex = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (data_hex != NULL);
	data = g_bytes_get_data (data_hex, &len);
	str = g_strndup ((const gchar *) data, len);
	g_assert_cmpstr (str, ==,
			 ":104000003DEF20F000000000FACF01F0FBCF02F0FE\n"
			 ":10401000E9CF03F0EACF04F0E1CF05F0E2CF06F0FC\n"
			 ":10402000D9CF07F0DACF08F0F3CF09F0F4CF0AF0D8\n"
			 ":10403000F6CF0BF0F7CF0CF0F8CF0DF0F5CF0EF078\n"
			 ":104040000EC0F5FF0DC0F8FF0CC0F7FF0BC0F6FF68\n"
			 ":104050000AC0F4FF09C0F3FF08C0DAFF07C0D9FFA8\n"
			 ":1040600006C0E2FF05C0E1FF04C0EAFF03C0E9FFAC\n"
			 ":1040700002C0FBFF01C0FAFF11003FEF20F000017A\n"
			 ":0840800042EF20F03DEF20F0BB\n"
			 ":00000001FF\n");

	/* do we match the binary file again */
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_RAW);
	data_bin2 = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (data_bin2 != NULL);
	g_assert_cmpstr (_g_bytes_compare_verbose (data_bin, data_bin2), ==, NULL);
}

static void
dfu_firmware_intel_hex_signed_func (void)
{
	DfuElement *element;
	DfuImage *image;
	GBytes *data_sig;
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *filename_hex = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_hex = NULL;

	/* load a Intel hex32 file */
	filename_hex = dfu_test_get_filename ("firmware.shex");
	g_assert (filename_hex != NULL);
	file_hex = g_file_new_for_path (filename_hex);
	firmware = dfu_firmware_new ();
	ret = dfu_firmware_parse_file (firmware, file_hex,
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 144);

	/* get the signed image element */
	image = dfu_firmware_get_image_by_name (firmware, "signature");
	g_assert (image != NULL);
	element = dfu_image_get_element_default	(image);
	data_sig = dfu_element_get_contents (element);
	g_assert (data_sig != NULL);
	data = g_bytes_get_data (data_sig, &len);
	g_assert_cmpint (len, ==, 8);
	g_assert (data != NULL);
}

static void
dfu_device_func (void)
{
	GPtrArray *targets;
	gboolean ret;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuTarget) target1 = NULL;
	g_autoptr(DfuTarget) target2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;

	/* find any DFU in appIDLE mode */
	usb_ctx = g_usb_context_new (&error);
	g_assert_no_error (error);
	g_assert (usb_ctx != NULL);
	g_usb_context_enumerate (usb_ctx);
	usb_device = g_usb_context_find_by_vid_pid (usb_ctx,
						    0x273f,
						    0x1005,
						    &error);
	if (usb_device == NULL)
		return;
	g_assert_no_error (error);
	g_assert (usb_device != NULL);

	/* check it's DFU-capable */
	device = dfu_device_new ();
	ret = dfu_device_set_new_usb_dev (device, usb_device, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get targets */
	targets = dfu_device_get_targets (device);
	g_assert_cmpint (targets->len, ==, 2);

	/* get by ID */
	target1 = dfu_device_get_target_by_alt_setting (device, 1, &error);
	g_assert_no_error (error);
	g_assert (target1 != NULL);

	/* ensure open */
	ret = dfu_device_open (device, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get by name */
	target2 = dfu_device_get_target_by_alt_name (device, "sram", &error);
	g_assert_no_error (error);
	g_assert (target2 != NULL);

	/* close */
	ret = dfu_device_close (device, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
dfu_device_dfu_v11 (void)
{
	gboolean ret;
	g_autoptr(DfuContext) context = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;

	/* create context */
	context = dfu_context_new ();
	ret = dfu_context_enumerate (context, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* does device exist */
	device = dfu_context_get_device_by_vid_pid (context,
						    0x273f,
						    0x100a,
						    &error);
	if (device == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
		g_test_skip ("no ColorHugDFU, skipping");
		return;
	}
	g_assert_no_error (error);
	g_assert (device != NULL);

	/* read contents */
	ret = dfu_device_open (device, &error);
	if (!ret &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_PERMISSION_DENIED)) {
		g_test_skip ("no permissions, skipping");
		return;
	}
	g_assert_no_error (error);
	g_assert (ret);
	firmware = dfu_device_upload (device, DFU_TARGET_TRANSFER_FLAG_NONE,
				      NULL, &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 16384);
}

static void
dfu_device_dfu_avr32 (void)
{
	gboolean ret;
	g_autoptr(DfuContext) context = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;

	/* create context */
	context = dfu_context_new ();
	ret = dfu_context_enumerate (context, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* does device exist */
	device = dfu_context_get_device_by_vid_pid (context,
						    0x03eb,
						    0x2ff1,
						    &error);
	if (device == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
		g_test_skip ("no UC3-A3 Xplained, skipping");
		return;
	}
	g_assert_no_error (error);
	g_assert (device != NULL);

	/* read contents */
	ret = dfu_device_open (device, &error);
	if (!ret &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_PERMISSION_DENIED)) {
		g_test_skip ("no permissions, skipping");
		return;
	}
	g_assert_no_error (error);
	g_assert (ret);
	firmware = dfu_device_upload (device, DFU_TARGET_TRANSFER_FLAG_NONE,
				      NULL, &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 11264);
}

static void
dfu_device_dfu_xmega (void)
{
	gboolean ret;
	g_autoptr(DfuContext) context = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;

	/* create context */
	context = dfu_context_new ();
	ret = dfu_context_enumerate (context, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* does device exist */
	device = dfu_context_get_device_by_vid_pid (context,
						    0x03eb,
						    0x2fe2,
						    &error);
	if (device == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
		g_test_skip ("no XMEGA-A3BU Xplained, skipping");
		return;
	}
	g_assert_no_error (error);
	g_assert (device != NULL);

	/* read contents */
	ret = dfu_device_open (device, &error);
	if (!ret &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_PERMISSION_DENIED)) {
		g_test_skip ("no permissions, skipping");
		return;
	}
	g_assert_no_error (error);
	g_assert (ret);
	firmware = dfu_device_upload (device, DFU_TARGET_TRANSFER_FLAG_NONE,
				      NULL, &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 29696);
}

static void
dfu_colorhug_plus_func (void)
{
	GPtrArray *elements;
	gboolean ret;
	gboolean seen_app_idle = FALSE;
	g_autoptr(DfuContext) context = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuDevice) device2 = NULL;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(GError) error = NULL;

	/* create context */
	context = dfu_context_new ();
	ret = dfu_context_enumerate (context, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* push appIDLE into dfuIDLE */
	device2 = dfu_context_get_device_by_vid_pid (context,
						     0x273f,
						     0x1002,
						     NULL);
	if (device2 != NULL) {
		ret = dfu_device_open (device2, &error);
		g_assert_no_error (error);
		g_assert (ret);
		ret = dfu_device_detach (device2, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);

		/* wait for it to come back as 273f:1005 */
		ret = dfu_device_wait_for_replug (device2, 5000, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);

		/* close it */
		ret = dfu_device_close (device2, &error);
		g_assert_no_error (error);
		g_assert (ret);
	}

	/* find any DFU in dfuIDLE mode */
	device = dfu_context_get_device_by_vid_pid (context,
						    0x273f,
						    0x1003,
						    NULL);
	if (device == NULL)
		return;

	/* we don't know this unless we went from appIDLE -> dfuIDLE */
	if (device2 == NULL) {
		g_assert_cmpint (dfu_device_get_runtime_vid (device), ==, 0xffff);
		g_assert_cmpint (dfu_device_get_runtime_pid (device), ==, 0xffff);
	}

	/* open it */
	ret = dfu_device_open (device, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* is in dfuIDLE mode */
	g_assert_cmpstr (dfu_state_to_string (dfu_device_get_state (device)), ==, "dfuIDLE");

	/* lets try and flash something inappropriate */
	if (seen_app_idle) {
		g_autoptr(DfuFirmware) firmware = NULL;
		g_autoptr(GFile) file = NULL;
		g_autofree gchar *filename = NULL;

		filename = dfu_test_get_filename ("kiibohd.dfu.bin");
		g_assert (filename != NULL);
		file = g_file_new_for_path (filename);
		firmware = dfu_firmware_new ();
		ret = dfu_firmware_parse_file (firmware, file,
					       DFU_FIRMWARE_PARSE_FLAG_NONE,
					       NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);
		ret = dfu_device_download (device, firmware,
					   DFU_TARGET_TRANSFER_FLAG_DETACH |
					   DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME,
					   NULL, &error);
		g_assert_error (error,
				FWUPD_ERROR,
				FWUPD_ERROR_INTERNAL);
		g_assert (ret);
		g_clear_error (&error);
	}

	/* get a dump of the existing firmware */
	target = dfu_device_get_target_by_alt_setting (device, 0, &error);
	g_assert_no_error (error);
	g_assert (target != NULL);
	image = dfu_target_upload (target, DFU_TARGET_TRANSFER_FLAG_NONE,
				   NULL, &error);
	g_assert_no_error (error);
	g_assert (DFU_IS_IMAGE (image));
	elements = dfu_image_get_elements (image);
	g_assert (elements != NULL);
	g_assert_cmpint (elements->len, ==, 1);

	/* download a new firmware */
	ret = dfu_target_download (target, image,
				   DFU_TARGET_TRANSFER_FLAG_VERIFY |
				   DFU_TARGET_TRANSFER_FLAG_ATTACH,
				   NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* wait for it to come back as 273f:1004 */
	ret = dfu_device_wait_for_replug (device, 5000, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* we should know now */
	g_assert_cmpint (dfu_device_get_runtime_vid (device), ==, 0x273f);
	g_assert_cmpint (dfu_device_get_runtime_pid (device), ==, 0x1002);
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
	ret = dfu_test_compare_lines (tmp,
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
	ret = dfu_test_compare_lines (tmp,
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
	ret = dfu_test_compare_lines (tmp,
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

	/* indicate a cipher being used */
	g_assert_cmpint (dfu_target_get_cipher_kind (target), ==, DFU_CIPHER_KIND_NONE);
	ret = dfu_target_parse_sectors (target, "@Flash|XTEA", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_target_get_cipher_kind (target), ==, DFU_CIPHER_KIND_XTEA);
}

static gboolean
dfu_patch_create_from_strings (DfuPatch *patch,
			       const gchar *dold,
			       const gchar *dnew,
			       GError **error)
{
	guint32 sz1 = strlen (dold);
	guint32 sz2 = strlen (dnew);
	g_autoptr(GBytes) blob1 = g_bytes_new (dold, sz1);
	g_autoptr(GBytes) blob2 = g_bytes_new (dnew, sz2);
	g_debug ("compare:\n%s\n%s", dold, dnew);
	return dfu_patch_create (patch, blob1, blob2, error);
}

static void
dfu_patch_merges_func (void)
{
	const guint8 *data;
	gboolean ret;
	gsize sz;
	g_autoptr(DfuPatch) patch = dfu_patch_new ();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* check merges happen */
	ret = dfu_patch_create_from_strings (patch, "XXX", "YXY", &error);
	g_assert_no_error (error);
	g_assert (ret);
	blob = dfu_patch_export (patch, &error);
	g_assert_no_error (error);
	g_assert (ret);
	data = g_bytes_get_data (blob, &sz);
	g_assert_cmpint (data[0x00], ==, 'D');
	g_assert_cmpint (data[0x01], ==, 'f');
	g_assert_cmpint (data[0x02], ==, 'u');
	g_assert_cmpint (data[0x03], ==, 'P');
	g_assert_cmpint (data[0x04], ==, 0x00); /* reserved */
	g_assert_cmpint (data[0x05], ==, 0x00);
	g_assert_cmpint (data[0x06], ==, 0x00);
	g_assert_cmpint (data[0x07], ==, 0x00);
	g_assert_cmpint (data[0x08 + 0x28], ==, 0x00); /* chunk1, offset */
	g_assert_cmpint (data[0x09 + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0a + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0b + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0c + 0x28], ==, 0x03); /* chunk1, size */
	g_assert_cmpint (data[0x0d + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0e + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0f + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x10 + 0x28], ==, 0x00); /* reserved */
	g_assert_cmpint (data[0x11 + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x12 + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x13 + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x14 + 0x28], ==, 'Y');
	g_assert_cmpint (data[0x15 + 0x28], ==, 'X');
	g_assert_cmpint (data[0x16 + 0x28], ==, 'Y');
	g_assert_cmpint (sz, ==, 48 /* hdr */ + 12 /* chunk */ + 3 /* data */);
}

static void
dfu_patch_apply_func (void)
{
	gboolean ret;
	g_autoptr(DfuPatch) patch = dfu_patch_new ();
	g_autoptr(GBytes) blob_new2 = NULL;
	g_autoptr(GBytes) blob_new3 = NULL;
	g_autoptr(GBytes) blob_new4 = NULL;
	g_autoptr(GBytes) blob_new = NULL;
	g_autoptr(GBytes) blob_old = NULL;
	g_autoptr(GBytes) blob_wrong = NULL;
	g_autoptr(GError) error = NULL;

	/* create a patch */
	blob_old = g_bytes_new_static ("helloworldhelloworldhelloworldhelloworld", 40);
	blob_new = g_bytes_new_static ("XelloXorldhelloworldhelloworldhelloworlXXX", 42);
	ret = dfu_patch_create (patch, blob_old, blob_new, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* apply the patch */
	blob_new2 = dfu_patch_apply (patch, blob_old, DFU_PATCH_APPLY_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (blob_new2 != NULL);
	g_assert_cmpint (g_bytes_compare (blob_new, blob_new2), ==, 0);

	/* check we force the patch to an unrelated blob */
	blob_wrong = g_bytes_new_static ("wrongwrongwrongwrongwrongwrongwrongwrong", 40);
	blob_new3 = dfu_patch_apply (patch, blob_wrong, DFU_PATCH_APPLY_FLAG_IGNORE_CHECKSUM, &error);
	g_assert_no_error (error);
	g_assert (blob_new3 != NULL);

	/* check we can't apply the patch to an unrelated blob */
	blob_new4 = dfu_patch_apply (patch, blob_wrong, DFU_PATCH_APPLY_FLAG_NONE, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert (blob_new4 == NULL);
}

static void
dfu_patch_func (void)
{
	const guint8 *data;
	gboolean ret;
	gsize sz;
	g_autoptr(DfuPatch) patch = dfu_patch_new ();
	g_autoptr(DfuPatch) patch2 = dfu_patch_new ();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *serialized_str = NULL;

	/* create binary diff */
	ret = dfu_patch_create_from_strings (patch, "XXX", "XYY", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check we can serialize this object to a blob */
	blob = dfu_patch_export (patch, &error);
	g_assert_no_error (error);
	g_assert (ret);
	data = g_bytes_get_data (blob, &sz);
	g_assert_cmpint (data[0x00], ==, 'D');
	g_assert_cmpint (data[0x01], ==, 'f');
	g_assert_cmpint (data[0x02], ==, 'u');
	g_assert_cmpint (data[0x03], ==, 'P');
	g_assert_cmpint (data[0x04], ==, 0x00); /* reserved */
	g_assert_cmpint (data[0x05], ==, 0x00);
	g_assert_cmpint (data[0x06], ==, 0x00);
	g_assert_cmpint (data[0x07], ==, 0x00);
	g_assert_cmpint (data[0x08 + 0x28], ==, 0x01); /* chunk1, offset */
	g_assert_cmpint (data[0x09 + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0a + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0b + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0c + 0x28], ==, 0x02); /* chunk1, size */
	g_assert_cmpint (data[0x0d + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0e + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x0f + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x10 + 0x28], ==, 0x00); /* reserved */
	g_assert_cmpint (data[0x11 + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x12 + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x13 + 0x28], ==, 0x00);
	g_assert_cmpint (data[0x14 + 0x28], ==, 'Y');
	g_assert_cmpint (data[0x15 + 0x28], ==, 'Y');
	g_assert_cmpint (sz, ==, 48 /* hdr */ + 12 /* chunk */ + 2 /* data */);

	/* try to load it from the serialized blob */
	ret = dfu_patch_import (patch2, blob, &error);
	g_assert_no_error (error);
	g_assert (ret);
	serialized_str = dfu_patch_to_string (patch2);
	g_debug ("serialized blob %s", serialized_str);
}

static void
dfu_chunked_func (void)
{
	g_autofree gchar *chunked1_str = NULL;
	g_autofree gchar *chunked2_str = NULL;
	g_autofree gchar *chunked3_str = NULL;
	g_autofree gchar *chunked4_str = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;
	g_autoptr(GPtrArray) chunked3 = NULL;
	g_autoptr(GPtrArray) chunked4 = NULL;

	chunked3 = dfu_chunked_new ((const guint8 *) "123456", 6, 0x0, 3, 3);
	chunked3_str = dfu_chunked_to_string (chunked3);
	g_print ("\n%s", chunked3_str);
	g_assert_cmpstr (chunked3_str, ==, "#00: page:00 addr:0000 len:03 123\n"
					   "#01: page:01 addr:0000 len:03 456\n");

	chunked4 = dfu_chunked_new ((const guint8 *) "123456", 6, 0x4, 4, 4);
	chunked4_str = dfu_chunked_to_string (chunked4);
	g_print ("\n%s", chunked4_str);
	g_assert_cmpstr (chunked4_str, ==, "#00: page:01 addr:0000 len:04 1234\n"
					   "#01: page:02 addr:0000 len:02 56\n");

	chunked1 = dfu_chunked_new ((const guint8 *) "0123456789abcdef", 16, 0x0, 10, 4);
	chunked1_str = dfu_chunked_to_string (chunked1);
	g_print ("\n%s", chunked1_str);
	g_assert_cmpstr (chunked1_str, ==, "#00: page:00 addr:0000 len:04 0123\n"
					   "#01: page:00 addr:0004 len:04 4567\n"
					   "#02: page:00 addr:0008 len:02 89\n"
					   "#03: page:01 addr:0000 len:04 abcd\n"
					   "#04: page:01 addr:0004 len:02 ef\n");

	chunked2 = dfu_chunked_new ((const guint8 *) "XXXXXXYYYYYYZZZZZZ", 18, 0x0, 6, 4);
	chunked2_str = dfu_chunked_to_string (chunked2);
	g_print ("\n%s", chunked2_str);
	g_assert_cmpstr (chunked2_str, ==, "#00: page:00 addr:0000 len:04 XXXX\n"
					   "#01: page:00 addr:0004 len:02 XX\n"
					   "#02: page:01 addr:0000 len:04 YYYY\n"
					   "#03: page:01 addr:0004 len:02 YY\n"
					   "#04: page:02 addr:0000 len:04 ZZZZ\n"
					   "#05: page:02 addr:0004 len:02 ZZ\n");
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
	g_test_add_func ("/dfu/chunked", dfu_chunked_func);
	g_test_add_func ("/dfu/patch", dfu_patch_func);
	g_test_add_func ("/dfu/patch{merges}", dfu_patch_merges_func);
	g_test_add_func ("/dfu/patch{apply}", dfu_patch_apply_func);
	g_test_add_func ("/dfu/enums", dfu_enums_func);
	g_test_add_func ("/dfu/target(DfuSe}", dfu_target_dfuse_func);
	g_test_add_func ("/dfu/cipher{xtea}", dfu_cipher_xtea_func);
	g_test_add_func ("/dfu/firmware{raw}", dfu_firmware_raw_func);
	g_test_add_func ("/dfu/firmware{dfu}", dfu_firmware_dfu_func);
	g_test_add_func ("/dfu/firmware{dfuse}", dfu_firmware_dfuse_func);
	g_test_add_func ("/dfu/firmware{xdfu}", dfu_firmware_xdfu_func);
	g_test_add_func ("/dfu/firmware{metadata}", dfu_firmware_metadata_func);
	g_test_add_func ("/dfu/firmware{intel-hex-offset}", dfu_firmware_intel_hex_offset_func);
	g_test_add_func ("/dfu/firmware{intel-hex}", dfu_firmware_intel_hex_func);
	g_test_add_func ("/dfu/firmware{intel-hex-signed}", dfu_firmware_intel_hex_signed_func);
	g_test_add_func ("/dfu/device", dfu_device_func);
	g_test_add_func ("/dfu/device{v1.1}", dfu_device_dfu_v11);
	g_test_add_func ("/dfu/device{avr32}", dfu_device_dfu_avr32);
	g_test_add_func ("/dfu/device{xmega}", dfu_device_dfu_xmega);
	g_test_add_func ("/dfu/colorhug+", dfu_colorhug_plus_func);
	return g_test_run ();
}

