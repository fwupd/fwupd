/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

int
main(int argc, char **argv)
{
	gboolean do_create = FALSE;
	g_autoptr(FuCabFirmware) cab_firmware = fu_cab_firmware_new();
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);
	g_autoptr(GError) error = NULL;

	const GOptionEntry options[] = {
	    {"create", 'c', 0, G_OPTION_ARG_NONE, &do_create, "Create archive", NULL},
	    {NULL}};

	g_option_context_add_main_entries(context, options, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Failed to parse arguments: %s\n", error->message);
		return EXIT_FAILURE;
	}

	if (do_create && argc > 1) {
		g_autoptr(GFile) file = g_file_new_for_path(argv[1]);
		for (gint i = 2; i < argc; i++) {
			g_autoptr(GBytes) img_blob = NULL;
			g_autoptr(FuCabImage) img = fu_cab_image_new();
			g_autofree gchar *basename = g_path_get_basename(argv[i]);

			fu_firmware_set_id(FU_FIRMWARE(img), basename);
			img_blob = fu_bytes_get_contents(argv[i], &error);
			if (img_blob == NULL) {
				g_printerr("Failed to load file %s: %s\n", argv[i], error->message);
				return EXIT_FAILURE;
			}
			fu_firmware_set_bytes(FU_FIRMWARE(img), img_blob);
			fu_firmware_add_image(FU_FIRMWARE(cab_firmware), FU_FIRMWARE(img));
		}
		if (!fu_firmware_write_file(FU_FIRMWARE(cab_firmware), file, &error)) {
			g_printerr("Failed to write file %s: %s\n", argv[1], error->message);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	g_printerr("Please specify a single operation\n");
	return EXIT_FAILURE;
}
