/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "fu-common.h"
#include "fu-wac-common.h"
#include "fu-wac-firmware.h"

#include "fwupd-error.h"

static void
fu_wac_firmware_parse_func (void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuFirmware) firmware = fu_wac_firmware_new ();
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GBytes) blob_block = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;

	/* parse the test file */
	fn = g_build_filename (TESTDATADIR, "test.wac", NULL);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
		g_test_skip ("no data file found");
		return;
	}
	bytes = fu_common_get_contents_bytes (fn, &error);
	g_assert_no_error (error);
	g_assert_nonnull (bytes);
	ret = fu_firmware_parse (firmware, bytes, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* get image data */
	img = fu_firmware_get_image_default (firmware, &error);
	g_assert_no_error (error);
	g_assert_nonnull (img);

	/* get block */
	blob_block = fu_firmware_image_write_chunk (img, 0x8008000, 1024, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_block);
	fu_wac_buffer_dump ("IMG", FU_WAC_REPORT_ID_MODULE,
			    g_bytes_get_data (blob_block, NULL),
			    g_bytes_get_size (blob_block));
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
	g_test_add_func ("/wac/firmware{parse}", fu_wac_firmware_parse_func);
	return g_test_run ();
}

