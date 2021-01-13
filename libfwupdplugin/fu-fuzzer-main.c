/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <glib.h>

__attribute__((weak)) extern int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);
__attribute__((weak)) extern int LLVMFuzzerInitialize(int *argc, char ***argv);

int
main (int argc, char **argv)
{
	g_assert (LLVMFuzzerTestOneInput != NULL);
	if (LLVMFuzzerInitialize != NULL)
		LLVMFuzzerInitialize (&argc, &argv);
	for (int i = 1; i < argc; i++) {
		gsize bufsz = 0;
		g_autofree gchar *buf = NULL;
		g_autoptr(GError) error = NULL;
		g_printerr ("Running: %s\n", argv[i]);
		if (!g_file_get_contents (argv[i], &buf, &bufsz, &error)) {
			g_printerr ("Failed to load: %s\n", error->message);
			continue;
		}
		LLVMFuzzerTestOneInput ((const guint8 *)buf, bufsz);
		g_printerr ("Done\n");
	}
}
