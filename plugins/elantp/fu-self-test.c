/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-elantp-firmware.h"

static void
fu_elantp_firmware_xml_func (void)
{
	gboolean ret;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_elantp_firmware_new ();
	g_autoptr(FuFirmware) firmware2 = fu_elantp_firmware_new ();
	g_autoptr(GError) error = NULL;

	/* build and write */
	ret = g_file_get_contents (FWUPD_FUZZINGSRCDIR "/elantp.builder.xml",
				   &xml_src, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_firmware_build_from_xml (firmware1, xml_src, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	csum1 = fu_firmware_get_checksum (firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (csum1, ==, "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");

	/* ensure we can round-trip */
	xml_out = fu_firmware_export_to_xml (firmware1,
					     FU_FIRMWARE_EXPORT_FLAG_NONE,
					     &error);
	g_assert_no_error (error);
	ret = fu_firmware_build_from_xml (firmware2, xml_out, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	csum2 = fu_firmware_get_checksum (firmware2, G_CHECKSUM_SHA1, &error);
	g_assert_cmpstr (csum1, ==, csum2);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/elantp/firmware{xml}", fu_elantp_firmware_xml_func);
	return g_test_run ();
}
