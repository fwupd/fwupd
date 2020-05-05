/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <stdlib.h>

#include "fu-uefi-dbx-file.h"

int
main (int argc, char *argv[])
{
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuUefiDbxFile) uefi_dbx_file = NULL;
	g_autoptr(GError) error = NULL;

	if (argc < 2) {
		g_printerr ("Not enough arguments, expected 'foo.bin'\n");
		return EXIT_FAILURE;
	}
	if (!g_file_get_contents (argv[1], (gchar **) &buf, &bufsz, &error)) {
		g_printerr ("Failed to load %s: %s\n", argv[1], error->message);
		return EXIT_FAILURE;
	}
	uefi_dbx_file = fu_uefi_dbx_file_new (buf, bufsz,
					      FU_UEFI_DBX_FILE_PARSE_FLAGS_IGNORE_HEADER,
					      &error);
	if (uefi_dbx_file == NULL) {
		g_printerr ("Failed to parse %s: %s\n", argv[1], error->message);
		return EXIT_FAILURE;
	}
	g_print ("%u checksums\n", fu_uefi_dbx_file_get_checksums(uefi_dbx_file)->len);
	return EXIT_SUCCESS;
}
