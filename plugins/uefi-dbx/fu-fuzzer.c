/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <stdlib.h>

#include "fu-efi-signature-parser.h"

int
main (int argc, char *argv[])
{
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) siglists = NULL;

	if (argc < 2) {
		g_printerr ("Not enough arguments, expected 'foo.bin'\n");
		return EXIT_FAILURE;
	}
	if (!g_file_get_contents (argv[1], (gchar **) &buf, &bufsz, &error)) {
		g_printerr ("Failed to load %s: %s\n", argv[1], error->message);
		return EXIT_FAILURE;
	}
	siglists = fu_efi_signature_parser_all (buf, bufsz,
						FU_EFI_SIGNATURE_PARSER_FLAGS_IGNORE_HEADER,
						&error);
	if (siglists == NULL) {
		g_printerr ("Failed to parse %s: %s\n", argv[1], error->message);
		return EXIT_FAILURE;
	}
	for (guint i = 0; i < siglists->len; i++) {
		FuEfiSignatureList *siglist = g_ptr_array_index (siglists, i);
		g_print ("%u checksums\n", fu_efi_signature_list_get_all(siglist)->len);
	}
	return EXIT_SUCCESS;
}
