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

#include <fwupd.h>
#include <glib-object.h>
#include <stdlib.h>

#include "dfu-common.h"
#include "dfu-device.h"
#include "dfu-firmware.h"
#include "dfu-target.h"

/**
 * dfu_test_get_filename:
 **/
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

/**
 * _g_bytes_compare_verbose:
 **/
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
		return g_strdup_printf ("got %li bytes, expected %li",
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
	gchar buf[256];
	guint i;
	gboolean ret;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) no_suffix_contents = NULL;
	g_autoptr(GBytes) roundtrip_orig = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;

	firmware = dfu_firmware_new ();

	/* set up some dummy data */
	for (i = 0; i < 256; i++)
		buf[i] = i;
	fw = g_bytes_new_static (buf, 256);

	/* load a non DFU firmware */
	ret = dfu_firmware_parse_data (firmware, fw, DFU_FIRMWARE_PARSE_FLAG_NONE, &error);
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

	firmware = dfu_firmware_new ();

	/* set up some dummy data */
	for (i = 0; i < 256; i++)
		buf[i] = i;
	fw = g_bytes_new_static (buf, 256);

	/* write DFU format */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU_1_0);
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
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_DFU_1_0);
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
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FIRMWARE_FORMAT_DFU_1_0);
	g_assert_cmpint (dfu_firmware_get_size (firmware), ==, 0x8eB4);

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
	ret = dfu_target_open (target1, DFU_TARGET_OPEN_FLAG_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get by name */
	target2 = dfu_device_get_target_by_alt_name (device, "sram", &error);
	g_assert_no_error (error);
	g_assert (target2 != NULL);

	/* close */
	ret = dfu_target_close (target1, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
dfu_colorhug_plus_func (void)
{
	gboolean ret;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;

	/* push appIDLE into dfuIDLE */
	usb_ctx = g_usb_context_new (&error);
	g_assert_no_error (error);
	g_assert (usb_ctx != NULL);
	g_usb_context_enumerate (usb_ctx);
	usb_device = g_usb_context_find_by_vid_pid (usb_ctx,
						    0x273f,
						    0x1002,
						    NULL);
	if (usb_device != NULL) {
		g_autoptr(DfuDevice) device2 = NULL;
		g_autoptr(DfuTarget) target2 = NULL;
		device2 = dfu_device_new (usb_device);
		g_assert (device2 != NULL);
		target2 = dfu_device_get_target_by_alt_setting (device2, 0, &error);
		g_assert_no_error (error);
		g_assert (target2 != NULL);
		ret = dfu_target_open (target2, DFU_TARGET_OPEN_FLAG_NO_AUTO_REFRESH, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);
		ret = dfu_target_detach (target2, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);

		/* wait for it to come back as 273f:1005 */
		ret = dfu_device_wait_for_replug (device2, 2000, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);

		/* close it */
		ret = dfu_target_close (target2, &error);
		g_assert_no_error (error);
		g_assert (ret);
		ret = dfu_device_close (device2, &error);
		g_assert_no_error (error);
		g_assert (ret);
	}

	/* find any DFU in dfuIDLE mode */
	usb_device = g_usb_context_find_by_vid_pid (usb_ctx,
						    0x273f,
						    0x1003,
						    &error);
	g_assert_no_error (error);
	g_assert (usb_device != NULL);

	/* check it's DFU-capable */
	device = dfu_device_new (usb_device);
	g_assert (device != NULL);
	target = dfu_device_get_target_by_alt_setting (device, 0, &error);
	g_assert_no_error (error);
	g_assert (target != NULL);

	/* we don't know this yet */
	g_assert_cmpint (dfu_device_get_runtime_vid (device), ==, 0xffff);
	g_assert_cmpint (dfu_device_get_runtime_pid (device), ==, 0xffff);

	/* open it */
	ret = dfu_target_open (target, DFU_TARGET_OPEN_FLAG_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* is in dfuIDLE mode */
	g_assert_cmpstr (dfu_state_to_string (dfu_target_get_state (target)), ==, "dfuIDLE");

	/* get a dump of the existing firmware */
	image = dfu_target_upload (target, 0, DFU_TARGET_TRANSFER_FLAG_NONE,
				   NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (DFU_IS_IMAGE (image));

	/* download a new firmware */
	ret = dfu_target_download (target, image,
				   DFU_TARGET_TRANSFER_FLAG_VERIFY |
				   DFU_TARGET_TRANSFER_FLAG_HOST_RESET,
				   NULL,
				   NULL, NULL,
				   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* wait for it to come back as 273f:1004 */
	ret = dfu_device_wait_for_replug (device, 2000, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* we should know now */
	g_assert_cmpint (dfu_device_get_runtime_vid (device), ==, 0x273f);
	g_assert_cmpint (dfu_device_get_runtime_pid (device), ==, 0x1002);

	/* close it */
	ret = dfu_target_close (target, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* close it again */
	ret = dfu_target_close (target, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/libdfu/enums", dfu_enums_func);
	g_test_add_func ("/libdfu/firmware{raw}", dfu_firmware_raw_func);
	g_test_add_func ("/libdfu/firmware{dfu}", dfu_firmware_dfu_func);
	g_test_add_func ("/libdfu/firmware{dfuse}", dfu_firmware_dfuse_func);
	g_test_add_func ("/libdfu/device", dfu_device_func);
	g_test_add_func ("/libdfu/colorhug+", dfu_colorhug_plus_func);
	return g_test_run ();
}

