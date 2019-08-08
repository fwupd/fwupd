/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-rmi-firmware.h"

static gboolean
fu_dump_parse (const gchar *filename, GError **error)
{
	gchar *data = NULL;
	gsize len = 0;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) firmware = fu_synaptics_rmi_firmware_new ();
	g_autofree gchar *str = NULL;
	if (!g_file_get_contents (filename, &data, &len, error))
		return FALSE;
	blob = g_bytes_new_take (data, len);
	if (!fu_firmware_parse (firmware, blob, FWUPD_INSTALL_FLAG_FORCE, error))
		return FALSE;
	str = fu_firmware_to_string (firmware);
	g_print ("%s", str);
	return TRUE;
}

static gboolean
fu_dump_generate_v0x (const gchar *filename, GError **error)
{
	const gchar *data;
	gsize len = 0;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fw = fu_synaptics_rmi_firmware_generate_v0x ();
	g_autoptr(FuFirmware) firmware = fu_synaptics_rmi_firmware_new ();
	g_autoptr(FuFirmwareImage) image = fu_firmware_image_new (fw);
	fu_firmware_add_image (firmware, image);
	blob = fu_firmware_write (firmware, error);
	if (blob == NULL)
		return FALSE;
	data = g_bytes_get_data (blob, &len);
	return g_file_set_contents (filename, data, len, error);
}

static gboolean
fu_dump_generate_v10 (const gchar *filename, GError **error)
{
	const gchar *data;
	gsize len = 0;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fw = fu_synaptics_rmi_firmware_generate_v10 ();
	g_autoptr(FuFirmware) firmware = fu_synaptics_rmi_firmware_new ();
	g_autoptr(FuFirmwareImage) image = fu_firmware_image_new (fw);
	fu_firmware_add_image (firmware, image);
	blob = fu_firmware_write (firmware, error);
	if (blob == NULL)
		return FALSE;
	data = g_bytes_get_data (blob, &len);
	return g_file_set_contents (filename, data, len, error);
}

int
main (int argc, char **argv)
{
	gint rc = 0;

	/* no args */
	if (argc <= 1) {
		g_printerr ("firmware filename required\n");
		return 2;
	}

	/* tool gen fn */
	if (argc == 3 && g_strcmp0 (argv[1], "gen0x") == 0) {
		g_autoptr(GError) error = NULL;
		if (!fu_dump_generate_v0x (argv[2], &error)) {
			g_printerr ("generate failed: %s\n", error->message);
			return 1;
		}
		return 0;
	}
	if (argc == 3 && g_strcmp0 (argv[1], "gen10") == 0) {
		g_autoptr(GError) error = NULL;
		if (!fu_dump_generate_v10 (argv[2], &error)) {
			g_printerr ("generate failed: %s\n", error->message);
			return 1;
		}
		return 0;
	}

	/* tool fn [fn2] [fn3] */
	for (gint i = 1; i < argc; i++) {
		g_autoptr(GError) error = NULL;
		if (!fu_dump_parse (argv[i], &error)) {
			g_printerr ("parse failed: %s\n", error->message);
			rc = 1;
		}
		if (rc != 0)
			return rc;
	}

	/* success */
	g_print ("OK!\n");
	return rc;
}
