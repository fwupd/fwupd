/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-efi-signature-private.h"
#include "fu-uefi-device-private.h"

static void
fu_uefi_device_read_default_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuUefiDevice) uefi_device = NULL;
	g_autoptr(FuEfiSignature) sig = fu_efi_signature_new(FU_EFI_SIGNATURE_KIND_SHA256);
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) csum = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* create a signature list of one SHA-256 signature for dbDefault */
	csum =
	    fu_bytes_from_string("0000000000000000000000000000000000000000000000000000000000000000",
				 &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum);
	fu_firmware_set_bytes(FU_FIRMWARE(sig), csum);
	ret = fu_firmware_add_image(siglist, FU_FIRMWARE(sig), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob = fu_firmware_write(siglist, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* set the dbDefault EFI variable */
	uefi_device = fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "db");
	fu_device_set_firmware_gtype(FU_DEVICE(uefi_device), FU_TYPE_EFI_SIGNATURE_LIST);
	ret = fu_uefi_device_set_efivar_bytes(uefi_device,
					      FU_EFIVARS_GUID_EFI_GLOBAL,
					      "dbDefault",
					      blob,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* read back the default */
	firmware =
	    fu_uefi_device_read_default(uefi_device, FU_EFIVARS_GUID_EFI_GLOBAL, "db", &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	g_assert_true(FU_IS_EFI_SIGNATURE_LIST(firmware));
	images = fu_firmware_get_images(firmware);
	g_assert_nonnull(images);
	g_assert_cmpuint(images->len, ==, 1);
}

static void
fu_uefi_device_read_default_missing_func(void)
{
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuUefiDevice) uefi_device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;

	/* no dbDefault set, should fail */
	uefi_device = fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "db");
	fu_device_set_firmware_gtype(FU_DEVICE(uefi_device), FU_TYPE_EFI_SIGNATURE_LIST);
	firmware =
	    fu_uefi_device_read_default(uefi_device, FU_EFIVARS_GUID_EFI_GLOBAL, "db", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(firmware);
}

static void
fu_uefi_device_auth_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuUefiDevice) uefi_device = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {0xFF};

	ret = fu_efivars_set_data(fu_context_get_efivars(ctx),
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "db",
				  buf,
				  sizeof(buf),
				  FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS |
				      FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				      FU_EFI_VARIABLE_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	uefi_device = fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "db");
	ret = fu_device_probe(FU_DEVICE(uefi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(
	    fu_device_has_problem(FU_DEVICE(uefi_device), FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM));
}

static void
fu_uefi_device_no_auth_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuUefiDevice) uefi_device = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {0xFF};

	ret = fu_efivars_set_data(fu_context_get_efivars(ctx),
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "db",
				  buf,
				  sizeof(buf),
				  FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS |
				      FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	uefi_device = fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "db");
	ret = fu_device_probe(FU_DEVICE(uefi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(
	    fu_device_has_problem(FU_DEVICE(uefi_device), FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/uefi-device/read-default", fu_uefi_device_read_default_func);
	g_test_add_func("/fwupd/uefi-device/read-default{missing}",
			fu_uefi_device_read_default_missing_func);
	g_test_add_func("/fwupd/uefi-device/auth", fu_uefi_device_auth_func);
	g_test_add_func("/fwupd/uefi-device/no-auth", fu_uefi_device_no_auth_func);
	return g_test_run();
}
