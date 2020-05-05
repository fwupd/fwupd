/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-uefi-dbx-common.h"
#include "fu-uefi-dbx-file.h"

static void
fu_uefi_dbx_file_parse_func (void)
{
	gboolean ret;
	gsize bufsz = 0;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuUefiDbxFile) uefi_dbx_file = NULL;
	g_autoptr(GError) error = NULL;

	/* load file */
	fn = fu_uefi_dbx_get_dbxupdate (NULL);
	if (fn == NULL) {
		g_test_skip ("no dbx file, use -Defi_dbxdir=");
		return;
	}
	ret = g_file_get_contents (fn, (gchar **) &buf, &bufsz, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* parse the update */
	uefi_dbx_file = fu_uefi_dbx_file_new (buf, bufsz,
					      FU_UEFI_DBX_FILE_PARSE_FLAGS_IGNORE_HEADER,
					      &error);
	g_assert_no_error (error);
	g_assert_nonnull (uefi_dbx_file);
	g_assert_cmpint (fu_uefi_dbx_file_get_checksums(uefi_dbx_file)->len, ==, 77);
	g_assert_true (fu_uefi_dbx_file_has_checksum (uefi_dbx_file, "72e0bd1867cf5d9d56ab158adf3bddbc82bf32a8d8aa1d8c5e2f6df29428d6d8"));
	g_assert_false (fu_uefi_dbx_file_has_checksum (uefi_dbx_file, "dave"));
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func ("/uefi-dbx/file-parse", fu_uefi_dbx_file_parse_func);
	return g_test_run ();
}
