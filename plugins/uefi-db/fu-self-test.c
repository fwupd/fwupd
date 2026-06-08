/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-efi-x509-signature-private.h"
#include "fu-uefi-db-device.h"
#include "fu-uefi-device-private.h"

static GBytes *
fu_uefi_db_self_test_build_siglist(GBytes *der, GError **error)
{
	g_autoptr(FuEfiX509Signature) sig = fu_efi_x509_signature_new();
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();

	fu_firmware_set_bytes(FU_FIRMWARE(sig), der);
	if (!fu_firmware_add_image(siglist, FU_FIRMWARE(sig), error))
		return NULL;
	return fu_firmware_write(siglist, error);
}

static void
fu_uefi_db_default_ids_func(void)
{
	gboolean ret;
	GPtrArray *children;
	gboolean found_crtd = FALSE;
	g_autofree gchar *quirk_fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuX509Certificate) crt_db = fu_x509_certificate_new();
	g_autoptr(FuX509Certificate) crt_default = fu_x509_certificate_new();
	g_autoptr(GBytes) blob_db = NULL;
	g_autoptr(GBytes) blob_default = NULL;
	g_autoptr(GBytes) der_db = NULL;
	g_autoptr(GBytes) der_default = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_GNUTLS
	g_test_skip("requires GnuTLS");
	return;
#endif

	/* set up quirks */
	tmpdir = fu_temporary_directory_new("uefi-db", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	quirk_fn = fu_temporary_directory_build(tmpdir, "uefi-db.quirk", NULL);
	ret = g_file_set_contents(quirk_fn,
				  "[93f84748-c854-5d6b-b78a-13c2361e0758]\n"
				  "Flags = use-db-default-ids\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_DATADIR_QUIRKS, tmpdir);

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	fu_hwids_add_guid(fu_context_get_hwids(ctx), "93f84748-c854-5d6b-b78a-13c2361e0758");
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_context_has_hwid_flag(ctx, "use-db-default-ids"));

	/* generate test certificates at runtime */
	fu_x509_certificate_set_subject(crt_db, "O=Test,CN=Test UEFI CA 2011");
	der_db = fu_firmware_write(FU_FIRMWARE(crt_db), &error);
	g_assert_no_error(error);
	g_assert_nonnull(der_db);
	fu_x509_certificate_set_subject(crt_default, "O=Test,CN=Test UEFI CA 2023");
	der_default = fu_firmware_write(FU_FIRMWARE(crt_default), &error);
	g_assert_no_error(error);
	g_assert_nonnull(der_default);

	/* build signature lists */
	blob_db = fu_uefi_db_self_test_build_siglist(der_db, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_db);
	blob_default = fu_uefi_db_self_test_build_siglist(der_default, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_default);

	/* create the device */
	device = g_object_new(FU_TYPE_UEFI_DB_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(device), FU_EFIVARS_GUID_SECURITY_DATABASE);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(device), "db");

	/* set the db EFI variable */
	ret = fu_uefi_device_set_efivar_bytes(
	    FU_UEFI_DEVICE(device),
	    FU_EFIVARS_GUID_SECURITY_DATABASE,
	    "db",
	    blob_db,
	    FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
		FU_EFI_VARIABLE_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set the dbDefault EFI variable */
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_EFI_GLOBAL,
					      "dbDefault",
					      blob_default,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* probe should create children with CRTD instance IDs */
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify at least one child has a CRTD instance ID */
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, >, 0);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		GPtrArray *instance_ids = fu_device_get_instance_ids(child);
		for (guint j = 0; j < instance_ids->len; j++) {
			const gchar *instance_id = g_ptr_array_index(instance_ids, j);
			if (g_strstr_len(instance_id, -1, "CRTD_") != NULL) {
				found_crtd = TRUE;
				break;
			}
		}
	}
	g_assert_true(found_crtd);
}

static void
fu_uefi_db_no_default_ids_func(void)
{
	gboolean ret;
	GPtrArray *children;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuX509Certificate) crt_db = fu_x509_certificate_new();
	g_autoptr(GBytes) blob_db = NULL;
	g_autoptr(GBytes) der_db = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_GNUTLS
	g_test_skip("requires GnuTLS");
	return;
#endif

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* flag should NOT be set */
	g_assert_false(fu_context_has_hwid_flag(ctx, "use-db-default-ids"));

	/* generate test certificate for db */
	fu_x509_certificate_set_subject(crt_db, "O=Test,CN=Test UEFI CA 2011");
	der_db = fu_firmware_write(FU_FIRMWARE(crt_db), &error);
	g_assert_no_error(error);
	g_assert_nonnull(der_db);
	blob_db = fu_uefi_db_self_test_build_siglist(der_db, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_db);

	/* create the device */
	device = g_object_new(FU_TYPE_UEFI_DB_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(device), FU_EFIVARS_GUID_SECURITY_DATABASE);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(device), "db");

	/* set the db EFI variable */
	ret = fu_uefi_device_set_efivar_bytes(
	    FU_UEFI_DEVICE(device),
	    FU_EFIVARS_GUID_SECURITY_DATABASE,
	    "db",
	    blob_db,
	    FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
		FU_EFI_VARIABLE_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* probe should create children without CRTD instance IDs */
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify no child has a CRTD instance ID */
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, >, 0);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		GPtrArray *instance_ids = fu_device_get_instance_ids(child);
		for (guint j = 0; j < instance_ids->len; j++) {
			const gchar *instance_id = g_ptr_array_index(instance_ids, j);
			g_assert_null(g_strstr_len(instance_id, -1, "CRTD_"));
		}
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/uefi-db/default-ids", fu_uefi_db_default_ids_func);
	g_test_add_func("/uefi-db/no-default-ids", fu_uefi_db_no_default_ids_func);
	return g_test_run();
}
