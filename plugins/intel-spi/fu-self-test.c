/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-efi-firmware-file.h"
#include "fu-efi-firmware-filesystem.h"
#include "fu-efi-firmware-section.h"
#include "fu-efi-firmware-volume.h"
#include "fu-ifd-bios.h"
#include "fu-ifd-image.h"

static void
fu_efi_firmware_section_xml_func (void)
{
	gboolean ret;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_efi_firmware_section_new ();
	g_autoptr(FuFirmware) firmware2 = fu_efi_firmware_section_new ();
	g_autoptr(GError) error = NULL;

	/* build and write */
	ret = g_file_get_contents (FWUPD_FUZZINGSRCDIR "/efi-firmware-section.builder.xml",
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

static void
fu_efi_firmware_file_xml_func (void)
{
	gboolean ret;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_efi_firmware_file_new ();
	g_autoptr(FuFirmware) firmware2 = fu_efi_firmware_file_new ();
	g_autoptr(GError) error = NULL;

	/* build and write */
	ret = g_file_get_contents (FWUPD_FUZZINGSRCDIR "/efi-firmware-file.builder.xml",
				   &xml_src, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_firmware_build_from_xml (firmware1, xml_src, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	csum1 = fu_firmware_get_checksum (firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (csum1, ==, "1002c14b29a76069f3b7e35c50a55d2b0d197441");

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

static void
fu_efi_firmware_filesystem_xml_func (void)
{
	gboolean ret;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_efi_firmware_filesystem_new ();
	g_autoptr(FuFirmware) firmware2 = fu_efi_firmware_filesystem_new ();
	g_autoptr(GError) error = NULL;

	/* build and write */
	ret = g_file_get_contents (FWUPD_FUZZINGSRCDIR "/efi-firmware-filesystem.builder.xml",
				   &xml_src, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_firmware_build_from_xml (firmware1, xml_src, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	csum1 = fu_firmware_get_checksum (firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (csum1, ==, "d6fbadc1c303a3b4eede9db7fb0ddb353efffc86");

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

static void
fu_efi_firmware_volume_xml_func (void)
{
	gboolean ret;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_efi_firmware_volume_new ();
	g_autoptr(FuFirmware) firmware2 = fu_efi_firmware_volume_new ();
	g_autoptr(GError) error = NULL;

	/* build and write */
	ret = g_file_get_contents (FWUPD_FUZZINGSRCDIR "/efi-firmware-volume.builder.xml",
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

static void
fu_ifd_image_xml_func (void)
{
	gboolean ret;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_ifd_image_new ();
	g_autoptr(FuFirmware) firmware2 = fu_ifd_image_new ();
	g_autoptr(GError) error = NULL;

	/* build and write */
	ret = g_file_get_contents (FWUPD_FUZZINGSRCDIR "/ifd.builder.xml",
				   &xml_src, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_firmware_build_from_xml (firmware1, xml_src, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	csum1 = fu_firmware_get_checksum (firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (csum1, ==, "aebfb3845c9bc638de30360f5ece156958918ca2");

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
	g_type_ensure (FU_TYPE_IFD_BIOS);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/efi/firmware-section{xml}", fu_efi_firmware_section_xml_func);
	g_test_add_func ("/efi/firmware-file{xml}", fu_efi_firmware_file_xml_func);
	g_test_add_func ("/efi/firmware-filesystem{xml}", fu_efi_firmware_filesystem_xml_func);
	g_test_add_func ("/efi/firmware-volume{xml}", fu_efi_firmware_volume_xml_func);
	g_test_add_func ("/ifd/image{xml}", fu_ifd_image_xml_func);
	return g_test_run ();
}
