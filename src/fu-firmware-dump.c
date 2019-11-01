/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-srec-firmware.h"
#include "fu-ihex-firmware.h"

int
main (int argc, char **argv)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	gsize sz = 0;
	const guint8 *buf;

	/* no args */
	if (argc != 2) {
		g_printerr ("firmware filename required\n");
		return 2;
	}

	/* load firmware */
	blob = fu_common_get_contents_bytes (argv[1], &error);
	if (blob == NULL) {
		g_printerr ("failed to load file: %s\n", error->message);
		return 1;
	}
	buf = g_bytes_get_data (blob, &sz);
	if (sz < 2) {
		g_printerr ("firmware invalid\n");
		return 2;
	}
	if (buf[0] == 'S' && buf[1] == '0') {
		firmware = fu_srec_firmware_new ();
	} else if (buf[0] == ':') {
		firmware = fu_ihex_firmware_new ();
	} else {
		g_printerr ("firmware invalid type, expected .srec or .hex\n");
		return 2;
	}
	if (!fu_firmware_parse (firmware, blob, FWUPD_INSTALL_FLAG_FORCE, &error)) {
		g_printerr ("failed to parse file: %s\n", error->message);
		return 3;
	}
	str = fu_firmware_to_string (firmware);
	g_print ("%s", str);
	return 0;
}
