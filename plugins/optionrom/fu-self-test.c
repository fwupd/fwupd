/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib/gstdio.h>
#include <gio/gfiledescriptorbased.h>
#include <stdlib.h>

#include "fu-plugin-private.h"
#include "fu-rom.h"

static void
fu_rom_func (void)
{
	struct {
		FuRomKind kind;
		const gchar *fn;
		const gchar *ver;
		guint16 vendor;
		guint16 model;
	} data[] = {
		    { FU_ROM_KIND_ATI,
			"Asus.9800PRO.256.unknown.031114.rom",
			"008.015.041.001",
			0x1002, 0x4e48 },
		    { FU_ROM_KIND_ATI, /* atombios */
			"Asus.R9290X.4096.131014.rom",
			"015.039.000.006.003515",
			0x1002, 0x67b0 },
		    { FU_ROM_KIND_ATI, /* atombios, with serial */
			"Asus.HD7970.3072.121018.rom",
			"015.023.000.002.000000",
			0x1002, 0x6798 },
		    { FU_ROM_KIND_NVIDIA,
			"Asus.GTX480.1536.100406_1.rom",
			"70.00.1A.00.02",
			0x10de, 0x06c0 },
		    { FU_ROM_KIND_NVIDIA, /* nvgi */
			"Asus.GTX980.4096.140905.rom",
			"84.04.1F.00.02",
			0x10de, 0x13c0 },
		    { FU_ROM_KIND_NVIDIA, /* nvgi, with serial */
			"Asus.TitanBlack.6144.140212.rom",
			"80.80.4E.00.01",
			0x10de, 0x100c },
		    { FU_ROM_KIND_UNKNOWN, NULL, NULL, 0x0000, 0x0000 }
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
		filename = g_build_filename (TESTDATADIR, data[i].fn, NULL);
		if (!g_file_test (filename, G_FILE_TEST_EXISTS))
			continue;
		g_print ("\nparsing %s...", filename);
		file = g_file_new_for_path (filename);
		ret = fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_BLANK_PPID, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert_cmpstr (fu_rom_get_version (rom), ==, data[i].ver);
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
	path = g_build_filename (TESTDATADIR, "roms", NULL);
	if (!g_file_test (path, G_FILE_TEST_EXISTS))
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
