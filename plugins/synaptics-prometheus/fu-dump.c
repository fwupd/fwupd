/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaprom-firmware.h"

static gboolean
fu_dump_parse (const gchar *filename, GError **error)
{
	gchar *data = NULL;
	gsize len = 0;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) firmware = fu_synaprom_firmware_new ();
	if (!g_file_get_contents (filename, &data, &len, error))
		return FALSE;
	blob = g_bytes_new_take (data, len);
	return fu_firmware_parse (firmware, blob, 0, error);
}

static gboolean
fu_dump_generate (const gchar *filename, GError **error)
{
	const gchar *data;
	gsize len = 0;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) firmware = fu_synaprom_firmware_new ();
	blob = fu_firmware_write (firmware, error);
	if (blob == NULL)
		return FALSE;
	data = g_bytes_get_data (blob, &len);
	return g_file_set_contents (filename, data, len, error);
}

int
main (int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	if (argc == 2) {
		if (!fu_dump_parse (argv[1], &error)) {
			g_printerr ("parse failed: %s\n", error->message);
			return 1;
		}
	} else if (argc == 3 && g_strcmp0 (argv[2], "gen") == 0) {
		if (!fu_dump_generate (argv[1], &error)) {
			g_printerr ("generate failed: %s\n", error->message);
			return 1;
		}
	} else {
		g_printerr ("firmware filename required\n");
		return 2;
	}
	g_print ("OK!\n");
	return 0;
}
