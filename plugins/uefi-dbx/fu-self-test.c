/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-uefi-dbx-common.h"
#include "fu-efi-signature-parser.h"

static void
fu_efi_signature_list_parse_func (void)
{
	FuEfiSignatureList *siglist;
	gboolean ret;
	gsize bufsz = 0;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GPtrArray) siglists = NULL;
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
	siglists = fu_efi_signature_parser_all (buf, bufsz,
						FU_EFI_SIGNATURE_PARSER_FLAGS_IGNORE_HEADER,
						&error);
	g_assert_no_error (error);
	g_assert_nonnull (siglists);
	g_assert_cmpint (siglists->len, ==, 1);
	siglist = g_ptr_array_index (siglists, 0);
	g_assert_cmpint (fu_efi_signature_list_get_all(siglist)->len, ==, 77);
	g_assert_true (fu_efi_signature_list_has_checksum (siglist, "72e0bd1867cf5d9d56ab158adf3bddbc82bf32a8d8aa1d8c5e2f6df29428d6d8"));
	g_assert_false (fu_efi_signature_list_has_checksum (siglist, "dave"));
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func ("/uefi-dbx/file-parse", fu_efi_signature_list_parse_func);
	return g_test_run ();
}
