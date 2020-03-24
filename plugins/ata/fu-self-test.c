/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-ata-device.h"
#include "fu-device-private.h"

static void
fu_ata_id_func (void)
{
	gboolean ret;
	gsize sz;
	g_autofree gchar *data = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(FuAtaDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	path = g_build_filename (TESTDATADIR, "StarDrive-SBFM61.2.bin", NULL);
	ret = g_file_get_contents (path, &data, &sz, &error);
	g_assert_no_error (error);
	g_assert (ret);
	dev = fu_ata_device_new_from_blob ((guint8 *)data, sz, &error);
	g_assert_no_error (error);
	g_assert_nonnull (dev);
	g_assert_cmpint (fu_ata_device_get_transfer_mode (dev), ==, 0xe);
	g_assert_cmpint (fu_ata_device_get_transfer_blocks (dev), ==, 0x1);
	g_assert_cmpstr (fu_device_get_serial (FU_DEVICE (dev)), ==, "A45A078A198600476509");
	g_assert_cmpstr (fu_device_get_name (FU_DEVICE (dev)), ==, "SATA SSD");
	g_assert_cmpstr (fu_device_get_version (FU_DEVICE (dev)), ==, "SBFM61.2");
}

static void
fu_ata_oui_func (void)
{
	gboolean ret;
	gsize sz;
	g_autofree gchar *data = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuAtaDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	path = g_build_filename (TESTDATADIR, "Samsung SSD 860 EVO 500GB.bin", NULL);
	ret = g_file_get_contents (path, &data, &sz, &error);
	g_assert_no_error (error);
	g_assert (ret);
	dev = fu_ata_device_new_from_blob ((guint8 *)data, sz, &error);
	g_assert_no_error (error);
	g_assert_nonnull (dev);
	fu_ata_device_set_unknown_oui_report (dev, FALSE);
	fu_device_convert_instance_ids (FU_DEVICE (dev));
	str = fu_device_to_string (FU_DEVICE (dev));
	g_debug ("%s", str);
	g_assert_cmpint (fu_ata_device_get_transfer_mode (dev), ==, 0xe);
	g_assert_cmpint (fu_ata_device_get_transfer_blocks (dev), ==, 0x1);
	g_assert_cmpstr (fu_device_get_serial (FU_DEVICE (dev)), ==, "S3Z1NB0K862928X");
	g_assert_cmpstr (fu_device_get_name (FU_DEVICE (dev)), ==, "SSD 860 EVO 500GB");
	g_assert_cmpstr (fu_device_get_version (FU_DEVICE (dev)), ==, "RVT01B6Q");
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/fwupd/ata/id", fu_ata_id_func);
	g_test_add_func ("/fwupd/ata/oui", fu_ata_oui_func);
	return g_test_run ();
}
