/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "fwupd-common-private.h"

#include "fu-rom.h"
#include "fu-common.h"

static gboolean
fu_fuzzer_rom_parse (const gchar *fn, GError **error)
{
	g_autoptr(FuRom) rom = NULL;
	g_autoptr(GFile) file = NULL;

	g_debug ("loading %s", fn);
	file = g_file_new_for_path (fn);
	rom = fu_rom_new ();
	if (!fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_NONE, NULL, error))
		return FALSE;
	g_print ("filename:%s\n", fn);
	g_print ("kind:%s\n", fu_rom_kind_to_string (fu_rom_get_kind (rom)));
	g_print ("version:%s\n", fu_rom_get_version (rom));
	g_print ("vendor:%u\n", fu_rom_get_vendor (rom));
	g_print ("model:%u\n\n", fu_rom_get_model (rom));
	return TRUE;
}

static gboolean
fu_fuzzer_write_files (GHashTable *hash, GError **error)
{
	GString *str;
	g_autoptr(GList) keys = g_hash_table_get_keys (hash);

	for (GList *l = keys; l != NULL; l = l->next) {
		g_autofree gchar *filename = NULL;
		const gchar *fn = l->data;
		filename = g_build_filename ("fuzzing", fn, NULL);
		str = g_hash_table_lookup (hash, fn);
		g_debug ("writing %s", fn);
		if (!g_file_set_contents (filename, str->str, str->len, error)) {
			g_prefix_error (error,
					"could not write file %s: ",
					filename);
			return FALSE;
		}
	}
	return TRUE;
}

static void
_g_string_unref (GString *str)
{
	g_string_free (str, TRUE);
}

static gboolean
fu_fuzzer_rom_create (GError **error)
{
	GString *str;
	guint8 *buffer;
	g_autofree guint8 *blob_header = NULL;
	g_autofree guint8 *blob_ifr = NULL;
	g_autoptr(GHashTable) hash = NULL;

	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      NULL,
				      (GDestroyNotify) _g_string_unref);

	/* 24 byte header */
	blob_header = g_malloc0 (0x200);
	buffer = blob_header;
	memcpy (buffer, "\x55\xaa", 2);
	buffer[0x02] = 1;			/* rom_len / 512 */
	buffer[0x03] = 0x20;			/* entry_point lo to blob just after header */
	buffer[0x04] = 'K';			/* entry_point hi (NVIDIA) */
	buffer[0x05] = '7';			/* entry_point higher (NVIDIA) */
	memcpy (&buffer[0x6], "xxxxxxxxxxxxxxxxxx", 18);	/* reserved */
	buffer[0x18] = 0x20;			/* cpi_ptr lo */
	buffer[0x19] = 0x00;			/* cpi_ptr hi */
	memcpy (&blob_header[0x6], "hdr-no-data       ", 18);
	g_hash_table_insert (hash, (gpointer) "header-no-data.rom",
			     g_string_new_len ((gchar *) blob_header, 512));

	/* data for header */
	buffer = &blob_header[0x20];
	memcpy (&buffer[0x00], "PCIR", 4);	/* magic */
	memcpy (&buffer[0x04], "\0\0", 2);	/* vendor */
	memcpy (&buffer[0x06], "\0\0", 2);	/* device id */
	memcpy (&buffer[0x08], "\0\0", 2);	/* device_list_ptr */
	buffer[0x0a] = 0x1c;			/* data_len lo */
	buffer[0x0b] = 0x00;			/* data_len hi */
	buffer[0x0c] = 0x0; 			/* data_rev */
	memcpy (&buffer[0x0d], "\0\0\0", 3);	/* class_code */
	buffer[0x10] = 0x01;			/* image_len lo / 512 */
	buffer[0x11] = 0;			/* image_len hi / 512 */
	buffer[0x12] = 0;			/* revision_level lo */
	buffer[0x13] = 0;			/* revision_level hi */
	buffer[0x14] = 0x00;			/* code_type, Intel x86 */
	buffer[0x15] = 0x80;			/* last_image */
	buffer[0x16] = 0x0;			/* max_runtime_len lo / 512 */
	buffer[0x17] = 0x0;			/* max_runtime_len hi / 512 */
	buffer[0x18] = 0x00;			/* config_header_ptr lo */
	buffer[0x19] = 0x00;			/* config_header_ptr hi */
	buffer[0x1a] = 0x00;			/* dmtf_clp_ptr lo (used for Intel FW) */
	buffer[0x1b] = 0x00;			/* dmtf_clp_ptr hi (used for Intel FW) */
	blob_header[0x200-1] = 0x5c;		/* checksum */

	/* blob */
	memcpy (&buffer[0x1c], "Version 1.0", 12);
	memcpy (&blob_header[0x6], "hdr-data-payload  ", 18);
	g_hash_table_insert (hash, (gpointer) "header-data-payload.rom",
			     g_string_new_len ((gchar *) blob_header, 512));

	/* optional IFR header on some NVIDIA blobs */
	blob_ifr = g_malloc0 (0x80);
	buffer = blob_ifr;
	memcpy (buffer, "NVGI", 4);
	fu_common_write_uint16 (&buffer[0x15], 0x80, G_BIG_ENDIAN);
	g_hash_table_insert (hash, (gpointer) "naked-ifr.rom",
			     g_string_new_len ((const gchar *) blob_ifr, 0x80));
	str = g_string_new_len ((gchar *) blob_ifr, 0x80);
	memcpy (&blob_header[0x6], (gpointer) "ifr-hdr-data-payld", 18);
	g_string_append_len (str, (gchar *) blob_header, 0x200);
	g_hash_table_insert (hash, (gpointer) "ifr-header-data-payload.rom", str);

	/* dump to files */
	return fu_fuzzer_write_files (hash, error);
}

int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	g_autoptr(GError) error_parse = NULL;
	g_autoptr(GOptionContext) context = NULL;
	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Run with debugging output enabled", NULL },
		{ NULL}
	};

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error_parse)) {
		g_print ("failed to parse command line arguments: %s\n",
			 error_parse->message);
		return EXIT_FAILURE;
	}

	if (argc < 3) {
		g_print ("Not enough arguments, expected 'rom' 'foo.rom'\n");
		return EXIT_FAILURE;
	}
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	if (g_strcmp0 (argv[1], "rom") == 0) {
		gboolean all_successful = TRUE;
		for (guint i = 2; i < (guint) argc; i++) {
			g_autoptr(GError) error = NULL;
			if (!fu_fuzzer_rom_parse (argv[i], &error)) {
				g_print ("Failed to parse %s: %s\n",
					 argv[i], error->message);
				all_successful = FALSE;
			}
		}
		return all_successful ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (g_strcmp0 (argv[1], "create") == 0) {
		g_autoptr(GError) error = NULL;
		if (!fu_fuzzer_rom_create (&error)) {
			g_print ("Failed to create files: %s\n", error->message);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	g_print ("Type not known: expected 'rom'\n");
	return EXIT_FAILURE;
}
