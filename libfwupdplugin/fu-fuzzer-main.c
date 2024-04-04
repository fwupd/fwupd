/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <glib.h>

__attribute__((weak)) extern int
LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);
__attribute__((weak)) extern int
LLVMFuzzerInitialize(int *argc, char ***argv);
__attribute__((weak)) extern GBytes *
BuildOneOutput(const gchar *filename, GError **error);

int
main(int argc, char **argv)
{
	g_assert_nonnull(LLVMFuzzerTestOneInput);
	if (LLVMFuzzerInitialize != NULL)
		LLVMFuzzerInitialize(&argc, &argv);

	/* do not use g_option_context_parse() here for speed */
	if (argc == 3 && g_str_has_suffix(argv[1], ".builder.xml") &&
	    g_str_has_suffix(argv[2], ".bin")) {
		g_autoptr(GBytes) blob_dst = NULL;
		g_autoptr(GError) error = NULL;
		blob_dst = BuildOneOutput(argv[1], &error);
		if (blob_dst == NULL) {
			g_printerr("Failed to build output: %s\n", error->message);
			return EXIT_FAILURE;
		}
		if (!g_file_set_contents(argv[2],
					 g_bytes_get_data(blob_dst, NULL),
					 g_bytes_get_size(blob_dst),
					 &error)) {
			g_printerr("Failed to save: %s\n", error->message);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	for (int i = 1; i < argc; i++) {
		gsize bufsz = 0;
		g_autofree gchar *buf = NULL;
		g_autoptr(GError) error = NULL;
		g_printerr("Running: %s\n", argv[i]);
		if (!g_file_get_contents(argv[i], &buf, &bufsz, &error)) {
			g_printerr("Failed to load: %s\n", error->message);
			continue;
		}
		LLVMFuzzerTestOneInput((const guint8 *)buf, bufsz);
		g_printerr("Done\n");
	}
	return EXIT_SUCCESS;
}
