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

static void
dfu_enums_func (void)
{
	guint i;
	for (i = 0; i < DFU_STATE_LAST; i++)
		g_assert_cmpstr (dfu_state_to_string (i), !=, NULL);
	for (i = 0; i < DFU_STATUS_LAST; i++)
		g_assert_cmpstr (dfu_status_to_string (i), !=, NULL);
}

static void
dfu_firmware_func (void)
{
	gchar buf[256];
	guint i;
	gboolean ret;
	g_autofree gchar *filename1 = NULL;
	g_autofree gchar *filename2 = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) no_suffix_contents = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file1 = NULL;
	g_autoptr(GFile) file2 = NULL;

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
	no_suffix_contents = dfu_firmware_get_contents (firmware);
	g_assert_cmpint (g_bytes_compare (no_suffix_contents, fw), ==, 0);

	/* write firmware format */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_vid (firmware, 0x1234);
	dfu_firmware_set_pid (firmware, 0x5678);
	dfu_firmware_set_release (firmware, 0xfedc);
	dfu_firmware_set_contents (firmware, fw);
	data = dfu_firmware_write_data (firmware, &error);
	g_assert_no_error (error);
	g_assert (data != NULL);

	/* can we load it again? */
	ret = dfu_firmware_parse_data (firmware, data, DFU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0x1234);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0x5678);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0xfedc);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FORMAT_DFU_1_0);

	/* load a real firmware */
	filename1 = dfu_test_get_filename ("kiibohd.dfu.bin");
	g_assert (filename1 != NULL);
	file1 = g_file_new_for_path (filename1);
	ret = dfu_firmware_parse_file (firmware, file1,
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0x1c11);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0xb007);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0xffff);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FORMAT_DFU_1_0);

	/* load a DeFUse firmware */
	filename2 = dfu_test_get_filename ("dev_VRBRAIN.dfu");
	g_assert (filename2 != NULL);
	file2 = g_file_new_for_path (filename2);
	ret = dfu_firmware_parse_file (firmware, file2,
				       DFU_FIRMWARE_PARSE_FLAG_NONE,
				       NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (dfu_firmware_get_vid (firmware), ==, 0x0483);
	g_assert_cmpint (dfu_firmware_get_pid (firmware), ==, 0x0000);
	g_assert_cmpint (dfu_firmware_get_release (firmware), ==, 0x0000);
	g_assert_cmpint (dfu_firmware_get_format (firmware), ==, DFU_FORMAT_DEFUSE);
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
	g_autoptr(GBytes) chunk = NULL;
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
		ret = dfu_target_wait_for_reset (target2, 2000, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);

		/* close it */
		ret = dfu_target_close (target2, &error);
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
	g_assert_cmpint (dfu_target_get_runtime_vid (target), ==, 0xffff);
	g_assert_cmpint (dfu_target_get_runtime_pid (target), ==, 0xffff);

	/* open it */
	ret = dfu_target_open (target, DFU_TARGET_OPEN_FLAG_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* is in dfuIDLE mode */
	g_assert_cmpstr (dfu_state_to_string (dfu_target_get_state (target)), ==, "dfuIDLE");

	/* get a dump of the existing firmware */
	chunk = dfu_target_upload (target, 0, DFU_TARGET_TRANSFER_FLAG_NONE,
				   NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (chunk != NULL);

	/* download a new firmware */
	ret = dfu_target_download (target, chunk,
				   DFU_TARGET_TRANSFER_FLAG_VERIFY |
				   DFU_TARGET_TRANSFER_FLAG_HOST_RESET,
				   NULL,
				   NULL, NULL,
				   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* wait for it to come back as 273f:1004 */
	ret = dfu_target_wait_for_reset (target, 2000, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* we should know now */
	g_assert_cmpint (dfu_target_get_runtime_vid (target), ==, 0x273f);
	g_assert_cmpint (dfu_target_get_runtime_pid (target), ==, 0x1002);

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
	g_test_add_func ("/libdfu/firmware", dfu_firmware_func);
	g_test_add_func ("/libdfu/device", dfu_device_func);
	g_test_add_func ("/libdfu/colorhug+", dfu_colorhug_plus_func);
	return g_test_run ();
}

