/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-dbx-common.h"

static void
fu_efi_image_func(void)
{
	struct {
		const gchar *basename;
		const gchar *checksum;
	} map[] = {
	    {"bootmgr.efi", "fd26aad248cc1e21e0c6b453212b2b309f7e221047bf22500ed0f8ce30bd1610"},
	    {"fwupdx64-2.efi", "6e0f01e7018c90a1e3d24908956fbeffd29a620c6c5f3ffa3feb2f2802ed4448"},
	};
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autofree gchar *csum = NULL;
		g_autofree gchar *fn = NULL;
		g_autoptr(FuFirmware) firmware = fu_pefile_firmware_new();
		g_autoptr(GError) error = NULL;
		g_autoptr(GFile) file = NULL;

		fn = g_test_build_filename(G_TEST_DIST, "tests", map[i].basename, NULL);
		file = g_file_new_for_path(fn);
		if (!g_file_query_exists(file, NULL)) {
			g_autofree gchar *msg =
			    g_strdup_printf("failed to find file %s", map[i].basename);
			g_test_skip(msg);
			return;
		}
		ret = fu_firmware_parse_file(firmware, file, FWUPD_INSTALL_FLAG_NONE, &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		csum = fu_firmware_get_checksum(firmware, G_CHECKSUM_SHA256, &error);
		g_assert_no_error(error);
		g_assert_nonnull(csum);
		g_assert_cmpstr(csum, ==, map[i].checksum);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/uefi-dbx/image", fu_efi_image_func);
	return g_test_run();
}
