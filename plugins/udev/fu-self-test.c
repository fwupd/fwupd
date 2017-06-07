/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gfiledescriptorbased.h>
#include <stdlib.h>

#include "fu-keyring.h"
#include "fu-pending.h"
#include "fu-plugin-private.h"
#include "fu-rom.h"
#include "fu-test.h"

static void
fu_rom_func (void)
{
	struct {
		FuRomKind kind;
		const gchar *fn;
		const gchar *ver;
		const gchar *csum;
		guint16 vendor;
		guint16 model;
	} data[] = {
		    { FU_ROM_KIND_ATI,
			"Asus.9800PRO.256.unknown.031114.rom",
			"008.015.041.001",
			"3137385685298bbf7db2c8304f60d89005c731ed",
			0x1002, 0x4e48 },
		    { FU_ROM_KIND_ATI, /* atombios */
			"Asus.R9290X.4096.131014.rom",
			"015.039.000.006.003515",
			"d8e32fa09a00ab9dcc96a990266f3fe5a99eacc5",
			0x1002, 0x67b0 },
		    { FU_ROM_KIND_ATI, /* atombios, with serial */
			"Asus.HD7970.3072.121018.rom",
			"015.023.000.002.000000",
			"ba8b6ce38f2499c8463fc9d983b8e0162b1121e4",
			0x1002, 0x6798 },
		    { FU_ROM_KIND_NVIDIA,
			"Asus.GTX480.1536.100406_1.rom",
			"70.00.1A.00.02",
			"3fcab24e60934850246fcfc4f42eceb32540a0ad",
			0x10de, 0x06c0 },
		    { FU_ROM_KIND_NVIDIA, /* nvgi */
			"Asus.GTX980.4096.140905.rom",
			"84.04.1F.00.02",
			"98f58321145bd347156455356bc04c5b04a292f5",
			0x10de, 0x13c0 },
		    { FU_ROM_KIND_NVIDIA, /* nvgi, with serial */
			"Asus.TitanBlack.6144.140212.rom",
			"80.80.4E.00.01",
			"3c80f35d4e3c440ffb427957d9271384113d7721",
			0x10de, 0x100c },
		    { FU_ROM_KIND_UNKNOWN, NULL, NULL, NULL, 0x0000, 0x0000 }
		};

	for (guint i = 0; data[i].fn != NULL; i++) {
		gboolean ret;
		g_autoptr(GError) error = NULL;
		g_autofree gchar *filename = NULL;
		g_autoptr(FuRom) rom = NULL;
		g_autoptr(GFile) file = NULL;
		rom = fu_rom_new ();
		g_assert (rom != NULL);

		/* load file */
		filename = fu_test_get_filename (TESTDATADIR, data[i].fn);
		if (filename == NULL)
			continue;
		g_print ("\nparsing %s...", filename);
		file = g_file_new_for_path (filename);
		ret = fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_BLANK_PPID, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert_cmpstr (fu_rom_get_version (rom), ==, data[i].ver);
		g_assert_cmpstr (g_ptr_array_index (fu_rom_get_checksums (rom), 0), ==, data[i].csum);
		g_assert_cmpint (fu_rom_get_kind (rom), ==, data[i].kind);
		g_assert_cmpint (fu_rom_get_vendor (rom), ==, data[i].vendor);
		g_assert_cmpint (fu_rom_get_model (rom), ==, data[i].model);
	}
}

static void
fu_rom_all_func (void)
{
	GDir *dir;
	g_autofree gchar *path = NULL;

	/* may or may not exist */
	path = fu_test_get_filename (TESTDATADIR, "roms");
	if (path == NULL)
		return;
	g_print ("\n");
	dir = g_dir_open (path, 0, NULL);
	do {
		const gchar *fn;
		gboolean ret;
		g_autoptr(GError) error = NULL;
		g_autofree gchar *filename = NULL;
		g_autoptr(FuRom) rom = NULL;
		g_autoptr(GFile) file = NULL;

		fn = g_dir_read_name (dir);
		if (fn == NULL)
			break;
		filename = g_build_filename (path, fn, NULL);
		g_print ("\nparsing %s...", filename);
		file = g_file_new_for_path (filename);
		rom = fu_rom_new ();
		ret = fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_BLANK_PPID, NULL, &error);
		if (!ret) {
			g_print ("%s %s : %s\n",
				 fu_rom_kind_to_string (fu_rom_get_kind (rom)),
				 filename, error->message);
			continue;
		}
		g_assert_cmpstr (fu_rom_get_version (rom), !=, NULL);
		g_assert_cmpstr (fu_rom_get_version (rom), !=, "\0");
		g_assert_cmpint (fu_rom_get_checksums(rom)->len, !=, 0);
		g_assert_cmpint (fu_rom_get_kind (rom), !=, FU_ROM_KIND_UNKNOWN);
	} while (TRUE);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/fwupd/rom", fu_rom_func);
	g_test_add_func ("/fwupd/rom{all}", fu_rom_all_func);
	return g_test_run ();
}
