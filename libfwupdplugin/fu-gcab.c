/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#endif

int
main(int argc, char **argv)
{
	gboolean do_compression = FALSE;
	gboolean do_create = FALSE;
	gboolean do_extract = FALSE;
	gboolean do_list = FALSE;
	gboolean no_path = FALSE;
	gboolean verbose = FALSE;
	g_autoptr(FuCabFirmware) cab_firmware = fu_cab_firmware_new();
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);
	g_autoptr(GError) error = NULL;

	const GOptionEntry options[] = {
	    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL},
	    {"create", 'c', 0, G_OPTION_ARG_NONE, &do_create, "Create archive", NULL},
	    {"extract", 'x', 0, G_OPTION_ARG_NONE, &do_extract, "Extract all files", NULL},
	    {"list", 'l', 0, G_OPTION_ARG_NONE, &do_list, "List contents", NULL},
	    {"zip", 'z', 0, G_OPTION_ARG_NONE, &do_compression, "Use zip compression", NULL},
	    {"nopath", 'n', 0, G_OPTION_ARG_NONE, &no_path, "Do not include path", NULL},
	    {NULL}};

#ifdef _WIN32
	/* required for Windows */
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	(void)g_setenv("LANG", "C.UTF-8", FALSE);
#endif

	setlocale(LC_ALL, "");

	g_option_context_add_main_entries(context, options, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Failed to parse arguments: %s\n", error->message);
		return EXIT_FAILURE;
	}
	if (verbose)
		(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	if (do_create && argc > 1) {
		g_autoptr(GFile) file = g_file_new_for_path(argv[1]);
		for (gint i = 2; i < argc; i++) {
			g_autoptr(GBytes) img_blob = NULL;
			g_autoptr(FuCabImage) img = fu_cab_image_new();
			if (no_path) {
				g_autofree gchar *basename = g_path_get_basename(argv[i]);
				fu_firmware_set_id(FU_FIRMWARE(img), basename);
			} else {
				fu_firmware_set_id(FU_FIRMWARE(img), argv[i]);
			}
			img_blob = fu_bytes_get_contents(argv[i], &error);
			if (img_blob == NULL) {
				g_printerr("Failed to load file %s: %s\n", argv[i], error->message);
				return EXIT_FAILURE;
			}
			fu_firmware_set_bytes(FU_FIRMWARE(img), img_blob);
			fu_firmware_add_image(FU_FIRMWARE(cab_firmware), FU_FIRMWARE(img));
		}
		if (do_compression)
			fu_cab_firmware_set_compressed(cab_firmware, TRUE);
		if (!fu_firmware_write_file(FU_FIRMWARE(cab_firmware), file, &error)) {
			g_printerr("Failed to write file %s: %s\n", argv[1], error->message);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}
	if (do_list && argc > 1) {
		g_autofree gchar *str = NULL;
		g_autoptr(GFile) file = g_file_new_for_path(argv[1]);
		if (!fu_firmware_parse_file(FU_FIRMWARE(cab_firmware),
					    file,
					    FWUPD_INSTALL_FLAG_NONE,
					    &error)) {
			g_printerr("Failed to parse %s: %s\n", argv[1], error->message);
			return EXIT_FAILURE;
		}
		str = fu_firmware_to_string(FU_FIRMWARE(cab_firmware));
		g_print("%s", str);
		return EXIT_SUCCESS;
	}

	g_printerr("Please specify a single operation\n");
	return EXIT_FAILURE;
}
