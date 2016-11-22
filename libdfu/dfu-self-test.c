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

#include "dfu-common.h"
#include "dfu-context.h"
#include "dfu-device.h"
#include "dfu-error.h"
#include "dfu-firmware.h"
#include "dfu-sector-private.h"
#include "dfu-target-private.h"

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
	guint i;

	data1 = g_bytes_get_data (bytes1, &length1);
	data2 = g_bytes_get_data (bytes2, &length2);

	/* not the same length */
	if (length1 != length2) {
		return g_strdup_printf ("got %" G_GSIZE_FORMAT " bytes, "
					"expected %" G_GSIZE_FORMAT,
					length1, length2);
	}

	/* return 00 01 02 03 */
	for (i = 0; i < length1; i++) {
		if (data1[i] != data2[i]) {
			return g_strdup_printf ("got 0x%02x, expected 0x%02x @ 0x%04x",
						data1[i], data2[i], i);
		}
	}
	return NULL;
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
	guint i;
	for (i = 0; i < DFU_STATE_LAST; i++)
		g_assert_cmpstr (dfu_state_to_string (i), !=, NULL);
	for (i = 0; i < DFU_STATUS_LAST; i++)
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
	guint i;
	gboolean ret;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) roundtrip_orig = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;

	/* set up some dummy data */
	for (i = 0; i < 256; i++)
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
	guint i;
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
	for (i = 0; i < 256; i++)
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
dfu_firmware_elf_func (void)
{
	DfuElement *element;
	DfuImage *image;
	GBytes *contents;
	const gchar *data;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GBytes) roundtrip_orig = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

#ifndef HAVE_LIBELF
	g_test_skip ("compiled without libelf support");
	return;
#endif

	/* load a ELF firmware */
	filename = dfu_test_get_filename ("example.elf");
	g_assert (filename != NULL);
	file = g_file_new_for_path (filename);
	firmware = dfu_firmware_new ();
	ret = dfu_firmware_parse_file (firmware, file,
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_ELF);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 0x0c);
	g_assert_cmpint (dfu_firmware_get_cipher_kind (firmware), ==, DFU_CIPHER_KIND_NONE);

	/* check the data */
	image = dfu_firmware_get_image_default (firmware);
	g_assert (image != NULL);
	element = dfu_image_get_element_default (image);
	g_assert (element != NULL);
	contents = dfu_element_get_contents (element);
	g_assert (contents != NULL);
	g_assert_cmpint (g_bytes_get_size (contents), ==, 12);
	data = g_bytes_get_data (contents, NULL);
	g_assert (data != NULL);
	g_assert (memcmp (data, "hello world\n", 12) == 0);

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
			 ":104000003DEF20F000000000FACF01F0FBCF02F0AF\n"
			 ":10401000E9CF03F0EACF04F0E1CF05F0E2CF06F005\n"
			 ":10402000D9CF07F0DACF08F0F3CF09F0F4CF0AF021\n"
			 ":10403000F6CF0BF0F7CF0CF0F8CF0DF0F5CF0EF044\n"
			 ":104040000EC0F5FF0DC0F8FF0CC0F7FF0BC0F6FF45\n"
			 ":104050000AC0F4FF09C0F3FF08C0DAFF07C0D9FF24\n"
			 ":1040600006C0E2FF05C0E1FF04C0EAFF03C0E9FF0A\n"
			 ":1040700002C0FBFF01C0FAFF11003FEF20F00001BB\n"
			 ":0840800042EF20F03DEF20F037\n"
			 ":00000001FF\n");

	/* do we match the binary file again */
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_RAW);
	data_bin2 = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (data_bin2 != NULL);
	g_assert_cmpstr (_g_bytes_compare_verbose (data_bin, data_bin2), ==, NULL);
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
	device = dfu_device_new (usb_device);
	g_assert (device != NULL);

	/* get targets */
	targets = dfu_device_get_targets (device);
	g_assert_cmpint (targets->len, ==, 2);

	/* get by ID */
	target1 = dfu_device_get_target_by_alt_setting (device, 1, &error);
	g_assert_no_error (error);
	g_assert (target1 != NULL);

	/* ensure open */
	ret = dfu_device_open (device, DFU_DEVICE_OPEN_FLAG_NONE, NULL, &error);
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
		ret = dfu_device_open (device2, DFU_DEVICE_OPEN_FLAG_NONE, NULL, &error);
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
	ret = dfu_device_open (device, DFU_DEVICE_OPEN_FLAG_NONE, NULL, &error);
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
				DFU_ERROR,
				DFU_ERROR_INTERNAL);
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
	DfuSector *sector;
	GPtrArray *sectors;
	GString *str;
	guint i;

	str = g_string_new ("");
	sectors = dfu_target_get_sectors (target);
	for (i = 0; i < sectors->len; i++) {
		g_autofree gchar *tmp = NULL;
		sector = g_ptr_array_index (sectors, i);
		tmp = dfu_sector_to_string (sector);
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

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* log everything */
	g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);
	g_setenv ("DFU_SELF_TEST", "", FALSE);

	/* tests go here */
	g_test_add_func ("/libdfu/enums", dfu_enums_func);
	g_test_add_func ("/libdfu/target(DfuSe}", dfu_target_dfuse_func);
	g_test_add_func ("/libdfu/firmware{raw}", dfu_firmware_raw_func);
	g_test_add_func ("/libdfu/firmware{dfu}", dfu_firmware_dfu_func);
	g_test_add_func ("/libdfu/firmware{dfuse}", dfu_firmware_dfuse_func);
	g_test_add_func ("/libdfu/firmware{xdfu}", dfu_firmware_xdfu_func);
	g_test_add_func ("/libdfu/firmware{metadata}", dfu_firmware_metadata_func);
	g_test_add_func ("/libdfu/firmware{intel-hex}", dfu_firmware_intel_hex_func);
	g_test_add_func ("/libdfu/firmware{elf}", dfu_firmware_elf_func);
	g_test_add_func ("/libdfu/device", dfu_device_func);
	g_test_add_func ("/libdfu/colorhug+", dfu_colorhug_plus_func);
	return g_test_run ();
}

