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
#include <stdlib.h>

#include "fu-cab.h"
#include "fu-cleanup.h"
#include "fu-keyring.h"
#include "fu-pending.h"
#include "fu-provider-fake.h"
#include "fu-rom.h"

/**
 * fu_test_get_filename:
 **/
static gchar *
fu_test_get_filename (const gchar *filename)
{
	gchar *tmp;
	char full_tmp[PATH_MAX];
	_cleanup_free_ gchar *path = NULL;
	path = g_build_filename (TESTDATADIR, filename, NULL);
	tmp = realpath (path, full_tmp);
	if (tmp == NULL)
		return NULL;
	return g_strdup (full_tmp);
}

static void
fu_rom_func (void)
{
	guint i;
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

	for (i = 0; data[i].fn != NULL; i++) {
		gboolean ret;
		_cleanup_error_free_ GError *error = NULL;
		_cleanup_free_ gchar *filename = NULL;
		_cleanup_object_unref_ FuRom *rom = NULL;
		_cleanup_object_unref_ GFile *file = NULL;
		rom = fu_rom_new ();
		g_assert (rom != NULL);

		/* load file */
		filename = fu_test_get_filename (data[i].fn);
		if (filename == NULL)
			continue;
		g_print ("\nparsing %s...", filename);
		file = g_file_new_for_path (filename);
		ret = fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_BLANK_PPID, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert_cmpstr (fu_rom_get_version (rom), ==, data[i].ver);
		g_assert_cmpstr (fu_rom_get_checksum (rom), ==, data[i].csum);
		g_assert_cmpint (fu_rom_get_kind (rom), ==, data[i].kind);
		g_assert_cmpint (fu_rom_get_vendor (rom), ==, data[i].vendor);
		g_assert_cmpint (fu_rom_get_model (rom), ==, data[i].model);
	}
}

static void
fu_rom_all_func (void)
{
	GDir *dir;
	_cleanup_free_ gchar *path = NULL;

	/* may or may not exist */
	path = fu_test_get_filename ("roms");
	if (path == NULL)
		return;
	g_print ("\n");
	dir = g_dir_open (path, 0, NULL);
	do {
		const gchar *fn;
		gboolean ret;
		_cleanup_error_free_ GError *error = NULL;
		_cleanup_free_ gchar *filename = NULL;
		_cleanup_object_unref_ FuRom *rom = NULL;
		_cleanup_object_unref_ GFile *file = NULL;

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
		g_assert_cmpstr (fu_rom_get_checksum (rom), !=, NULL);
		g_assert_cmpint (fu_rom_get_kind (rom), !=, FU_ROM_KIND_UNKNOWN);
	} while (TRUE);
}

static void
fu_cab_func (void)
{
	GError *error = NULL;
	gboolean ret;
	_cleanup_free_ gchar *filename = NULL;
	_cleanup_object_unref_ FuCab *cab = NULL;
	_cleanup_object_unref_ GFile *file = NULL;

	cab = fu_cab_new ();
	g_assert (cab != NULL);

	/* load file */
	filename = fu_test_get_filename ("colorhug-als-3.0.2.cab");
	g_assert (filename != NULL);
	file = g_file_new_for_path (filename);
	ret = fu_cab_load_file (cab, file, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get properties */
	g_assert (fu_cab_get_stream (cab) != NULL);
	g_assert_cmpstr (fu_cab_get_guid (cab), ==, "84f40464-9272-4ef7-9399-cd95f12da696");
	g_assert_cmpstr (fu_cab_get_version (cab), ==, "3.0.2");
	g_assert_cmpstr (fu_cab_get_url_homepage (cab), ==, "http://www.hughski.com/");
	g_assert_cmpstr (fu_cab_get_license (cab), ==, "GPL-2.0+");
	g_assert_cmpint (fu_cab_get_size (cab), ==, 10174);
	g_assert_cmpstr (fu_cab_get_description (cab), !=, NULL);
	g_assert_cmpint (fu_cab_get_trust_flags (cab), ==, FWUPD_TRUST_FLAG_NONE);
	g_assert (!g_file_test (fu_cab_get_filename_firmware (cab), G_FILE_TEST_EXISTS));

	/* extract firmware */
	ret = fu_cab_extract_firmware (cab, &error);
	/* this is not available in make distcheck */
	if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
		g_assert (ret);
		g_assert_cmpint (fu_cab_get_trust_flags (cab), ==, FWUPD_TRUST_FLAG_PAYLOAD);
	}
	g_assert (g_str_has_suffix (fu_cab_get_filename_firmware (cab), "/firmware.bin"));
	g_assert (g_file_test (fu_cab_get_filename_firmware (cab), G_FILE_TEST_EXISTS));

	/* clean up */
	ret = fu_cab_delete_temp_files (cab, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (!g_file_test (fu_cab_get_filename_firmware (cab), G_FILE_TEST_EXISTS));
}

static void
_provider_status_changed_cb (FuProvider *provider, FwupdStatus status, gpointer user_data)
{
	guint *cnt = (guint *) user_data;
	(*cnt)++;
}

static void
_provider_device_added_cb (FuProvider *provider, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **) user_data;
	*dev = g_object_ref (device);
}

static void
fu_provider_func (void)
{
	GError *error = NULL;
	FuDevice *device_tmp;
	gboolean ret;
	guint cnt = 0;
	_cleanup_free_ gchar *pending_cap = NULL;
	_cleanup_free_ gchar *pending_db = NULL;
	_cleanup_object_unref_ FuDevice *device = NULL;
	_cleanup_object_unref_ FuPending *pending = NULL;
	_cleanup_object_unref_ FuProvider *provider = NULL;
	_cleanup_object_unref_ GFile *file = NULL;
	_cleanup_object_unref_ GInputStream *stream = NULL;

	/* create a fake device */
	provider = fu_provider_fake_new ();
	g_signal_connect (provider, "device-added",
			  G_CALLBACK (_provider_device_added_cb),
			  &device);
	g_signal_connect (provider, "status-changed",
			  G_CALLBACK (_provider_status_changed_cb),
			  &cnt);
	ret = fu_provider_coldplug (provider, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check we did the right thing */
	g_assert_cmpint (cnt, ==, 0);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "FakeDevice");
	g_assert_cmpstr (fu_device_get_guid (device), ==,
			 "00000000-0000-0000-0000-000000000000");

	/* schedule an offline update */
	file = g_file_new_for_path ("/etc/resolv.conf");
	stream = G_INPUT_STREAM (g_file_read (file, NULL, &error));
	g_assert_no_error (error);
	g_assert (stream != NULL);
	ret = fu_provider_update (provider, device, stream, -1,
				  NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (cnt, ==, 1);

	/* lets check the pending */
	pending = fu_pending_new ();
	device_tmp = fu_pending_get_device (pending, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_metadata (device_tmp, FU_DEVICE_KEY_PENDING_STATE), ==, "scheduled");
	g_assert_cmpstr (fu_device_get_metadata (device_tmp, FU_DEVICE_KEY_PENDING_ERROR), ==, NULL);
	g_assert_cmpstr (fu_device_get_metadata (device_tmp, FU_DEVICE_KEY_FILENAME_CAB), !=, NULL);

	/* save this; we'll need to delete it later */
	pending_cap = g_strdup (fu_device_get_metadata (device_tmp, FU_DEVICE_KEY_FILENAME_CAB));
	g_object_unref (device_tmp);

	/* lets do this online */
	ret = fu_provider_update (provider, device, stream, -1,
				  FU_PROVIDER_UPDATE_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (cnt, ==, 3);

	/* lets check the pending */
	device_tmp = fu_pending_get_device (pending, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_metadata (device_tmp, FU_DEVICE_KEY_PENDING_STATE), ==, "success");
	g_assert_cmpstr (fu_device_get_metadata (device_tmp, FU_DEVICE_KEY_PENDING_ERROR), ==, NULL);
	g_object_unref (device_tmp);

	/* get the status */
	device_tmp = fu_device_new ();
	fu_device_set_id (device_tmp, "FakeDevice");
	ret = fu_provider_get_results (provider, device_tmp, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpstr (fu_device_get_metadata (device_tmp, FU_DEVICE_KEY_PENDING_STATE), ==, "success");
	g_assert_cmpstr (fu_device_get_metadata (device_tmp, FU_DEVICE_KEY_PENDING_ERROR), ==, NULL);

	/* clear */
	ret = fu_provider_clear_results (provider, device_tmp, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* re-get the status */
	ret = fu_provider_get_results (provider, device_tmp, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert (!ret);

	g_object_unref (device_tmp);
	g_clear_error (&error);

	/* delete files */
	pending_db = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", "pending.db", NULL);
	g_unlink (pending_db);
	g_unlink (pending_cap);
}

static void
fu_pending_func (void)
{
	GError *error = NULL;
	gboolean ret;
	FuDevice *device;
	_cleanup_object_unref_ FuPending *pending = NULL;
	_cleanup_free_ gchar *dirname = NULL;
	_cleanup_free_ gchar *filename = NULL;

	/* create */
	pending = fu_pending_new ();
	g_assert (pending != NULL);

	/* delete the database */
	dirname = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", NULL);
	if (!g_file_test (dirname, G_FILE_TEST_IS_DIR))
		return;
	filename = g_build_filename (dirname, "pending.db", NULL);
	g_unlink (filename);

	/* add a device */
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	fu_device_set_metadata (device, FU_DEVICE_KEY_FILENAME_CAB, "/var/lib/dave.cap"),
	fu_device_set_display_name (device, "ColorHug"),
	fu_device_set_metadata (device, FU_DEVICE_KEY_VERSION, "3.0.1"),
	fu_device_set_metadata (device, FU_DEVICE_KEY_VERSION_NEW, "3.0.2");
	ret = fu_pending_add_device (pending, device, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* ensure database was created */
	g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));

	/* add some extra data */
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	ret = fu_pending_set_state (pending, device, FU_PENDING_STATE_SCHEDULED, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_pending_set_error_msg (pending, device, "word", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* get device */
	device = fu_pending_get_device (pending, "self-test", &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "self-test");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_FILENAME_CAB), ==, "/var/lib/dave.cap");
	g_assert_cmpstr (fu_device_get_display_name (device), ==, "ColorHug");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_OLD), ==, "3.0.1");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_NEW), ==, "3.0.2");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_PENDING_STATE), ==, "scheduled");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_PENDING_ERROR), ==, "word");
	g_object_unref (device);

	/* get device that does not exist */
	device = fu_pending_get_device (pending, "XXXXXXXXXXXXX", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (device == NULL);
	g_clear_error (&error);

	/* remove device */
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	ret = fu_pending_remove_device (pending, device, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* get device that does not exist */
	device = fu_pending_get_device (pending, "self-test", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (device == NULL);
	g_clear_error (&error);
}

static void
fu_keyring_func (void)
{
	gboolean ret;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *fw_fail = NULL;
	_cleanup_free_ gchar *fw_pass = NULL;
	_cleanup_free_ gchar *pki_dir = NULL;
	_cleanup_object_unref_ FuKeyring *keyring = NULL;
	const gchar *sig =
	"iQEcBAABAgAGBQJVK9RSAAoJEBesuo36lw4XvmoH/3tJL5wVRN+rsvoo/FMc3w4g"
	"I7rizJNIgQ04WVTREX6tRZJfxYzGAaeokVeqah2JUC4u1j22BDkoG/Fs+/2/Z/OP"
	"PTxMoiEzfzryWpVwt20As+H9CmMZGdCfvKgnWiosAENCzE7JE1miJ4YvTpRtdPMh"
	"erz8DqLTFAfr72aimf5hBs8ZFkBGPGjljdTDv78hk2WDep5E1+1swGoFbhDcXyih"
	"8GZjSLP7XkKo23/p6odCJD3SkkDE7jIUMA8GrTHHXIhF41UsriKx2ERYoau5k3cX"
	"OdK3/cRQ6BeuSBMLr7hUpa0RwlKUKex/I7+p/T9Ohk4lNnGS7GpE45RbpflK1VQ="
	"=0D8+";

	/* add test keys to keyring */
	keyring = fu_keyring_new ();
	pki_dir = fu_test_get_filename ("pki");
	ret = fu_keyring_add_public_keys (keyring, pki_dir, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify */
	fw_pass = fu_test_get_filename ("firmware.bin");
	ret = fu_keyring_verify_file (keyring, fw_pass, sig, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify will fail */
	fw_fail = fu_test_get_filename ("colorhug-als-3.0.2.cab");
	ret = fu_keyring_verify_file (keyring, fw_fail, sig, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_assert (!ret);
	g_clear_error (&error);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func ("/fwupd/rom", fu_rom_func);
	g_test_add_func ("/fwupd/rom{all}", fu_rom_all_func);
	g_test_add_func ("/fwupd/cab", fu_cab_func);
	g_test_add_func ("/fwupd/pending", fu_pending_func);
	g_test_add_func ("/fwupd/provider", fu_provider_func);
	g_test_add_func ("/fwupd/keyring", fu_keyring_func);
	return g_test_run ();
}

