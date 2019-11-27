/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-device-private.h"
#include "fu-nvme-device.h"

static void
fu_nvme_cns_func (void)
{
	gboolean ret;
	gsize sz;
	g_autofree gchar *data = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuNvmeDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	path = g_build_filename (TESTDATADIR, "TOSHIBA_THNSN5512GPU7.bin", NULL);
	ret = g_file_get_contents (path, &data, &sz, &error);
	g_assert_no_error (error);
	g_assert (ret);
	dev = fu_nvme_device_new_from_blob ((guint8 *)data, sz, &error);
	g_assert_no_error (error);
	g_assert_nonnull (dev);
	fu_device_convert_instance_ids (FU_DEVICE (dev));
	g_assert_cmpstr (fu_device_get_name (FU_DEVICE (dev)), ==, "THNSN5512GPU7 TOSHIBA");
	g_assert_cmpstr (fu_device_get_version (FU_DEVICE (dev)), ==, "410557LA");
	g_assert_cmpstr (fu_device_get_serial (FU_DEVICE (dev)), ==, "37RSDEADBEEF");
	g_assert_cmpstr (fu_device_get_guid_default (FU_DEVICE (dev)), ==, "e1409b09-50cf-5aef-8ad8-760b9022f88d");
}

static void
fu_nvme_cns_all_func (void)
{
	const gchar *fn;
	g_autofree gchar *path = NULL;
	g_autoptr(GDir) dir = NULL;

	/* may or may not exist */
	path = g_build_filename (TESTDATADIR, "blobs", NULL);
	if (!g_file_test (path, G_FILE_TEST_EXISTS))
		return;
	dir = g_dir_open (path, 0, NULL);
	while ((fn = g_dir_read_name (dir)) != NULL) {
		gsize sz;
		g_autofree gchar *data = NULL;
		g_autofree gchar *filename = NULL;
		g_autoptr(FuNvmeDevice) dev = NULL;
		g_autoptr(GError) error = NULL;

		filename = g_build_filename (path, fn, NULL);
		g_print ("parsing %s... ", filename);
		if (!g_file_get_contents (filename, &data, &sz, &error)) {
			g_print ("failed to load %s: %s\n", filename, error->message);
			continue;
		}
		dev = fu_nvme_device_new_from_blob ((guint8 *) data, sz, &error);
		if (dev == NULL) {
			g_print ("failed to load %s: %s\n", filename, error->message);
			continue;
		}
		g_assert_cmpstr (fu_device_get_name (FU_DEVICE (dev)), !=, NULL);
		g_assert_cmpstr (fu_device_get_version (FU_DEVICE (dev)), !=, NULL);
		g_assert_cmpstr (fu_device_get_serial (FU_DEVICE (dev)), !=, NULL);
		g_print ("done\n");
	}
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/fwupd/cns", fu_nvme_cns_func);
	g_test_add_func ("/fwupd/cns{all}", fu_nvme_cns_all_func);
	return g_test_run ();
}
