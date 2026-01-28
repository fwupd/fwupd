/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-efi-signature-private.h"
#include "fu-uefi-dbx-device.h"
#include "fu-uefi-device-private.h"

static void
fu_uefi_dbx_zero_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	g_autoptr(FuEfiSignature) sig = fu_efi_signature_new(FU_EFI_SIGNATURE_KIND_SHA256);
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) csum = NULL;
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* zero hash = empty */
	csum =
	    fu_bytes_from_string("0000000000000000000000000000000000000000000000000000000000000000",
				 &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum);
	fu_firmware_set_bytes(FU_FIRMWARE(sig), csum);
	fu_firmware_add_image(siglist, FU_FIRMWARE(sig), NULL);
	blob = fu_firmware_write(siglist, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* create a plausible KEK */
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(device), "KEK");
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_EFI_GLOBAL,
					      "KEK",
					      blob,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create an "empty" dbx */
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_SECURITY_DATABASE,
					      "dbx",
					      blob,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* detect version number */
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_device_get_version_raw(device), ==, 0);
	g_assert_cmpstr(fu_device_get_version(device), ==, "0");
}

static void
fu_efi_image_func(void)
{
	struct {
		const gchar *basename;
		const gchar *checksum;
	} map[] = {
	    {"bootmgr.efi", "fd26aad248cc1e21e0c6b453212b2b309f7e221047bf22500ed0f8ce30bd1610"},
	    {"fwupdx64-2.efi", "6e0f01e7018c90a1e3d24908956fbeffd29a620c6c5f3ffa3feb2f2802ed4448"},
	};
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autofree gchar *csum = NULL;
		g_autofree gchar *fn = NULL;
		g_autoptr(FuFirmware) firmware = fu_pefile_firmware_new();
		g_autoptr(GError) error = NULL;
		g_autoptr(GFile) file = NULL;

		fn = g_test_build_filename(G_TEST_DIST, "tests", map[i].basename, NULL);
		file = g_file_new_for_path(fn);
		if (!g_file_query_exists(file, NULL)) {
			g_autofree gchar *msg =
			    g_strdup_printf("failed to find file %s", map[i].basename);
			g_test_skip(msg);
			return;
		}
		ret = fu_firmware_parse_file(firmware, file, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
		if (!ret)
			g_prefix_error(&error, "%s: ", map[i].basename);
		g_assert_no_error(error);
		g_assert_true(ret);

		csum = fu_firmware_get_checksum(firmware, G_CHECKSUM_SHA256, &error);
		g_assert_no_error(error);
		g_assert_nonnull(csum);
		g_assert_cmpstr(csum, ==, map[i].checksum);
	}
}

static void
fu_uefi_dbx_not_present_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) ms_blob = NULL;
	g_autoptr(FuFirmware) ms_siglist = NULL;
	g_autofree gchar *ms_kek_filename = NULL;
	g_autofree gchar *ms_kek_xml = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create a KEK with Microsoft's signature */
	ms_kek_filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "efi-signature-list.builder.xml", NULL);
	g_assert_nonnull(ms_kek_filename);

	ret = g_file_get_contents(ms_kek_filename, &ms_kek_xml, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ms_siglist = fu_firmware_new_from_xml(ms_kek_xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(ms_siglist);

	ms_blob = fu_firmware_write(ms_siglist, &error);
	g_assert_no_error(error);
	g_assert_nonnull(ms_blob);

	fu_uefi_device_set_guid(FU_UEFI_DEVICE(device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(device), "KEK");
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_EFI_GLOBAL,
					      "KEK",
					      ms_blob,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
	(void)g_setenv("FWUPD_EFIVARS", "dummy", TRUE);

	/* tests go here */
	g_test_add_func("/uefi-dbx/image", fu_efi_image_func);
	g_test_add_func("/uefi-dbx/zero", fu_uefi_dbx_zero_func);
	g_test_add_func("/uefi-dbx/not-present", fu_uefi_dbx_not_present_func);

	return g_test_run();
}
