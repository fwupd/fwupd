/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fwupd-security-attr-private.h"

#include "fu-context-private.h"
#include "fu-efi-x509-signature-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-uefi-db-device.h"
#include "fu-uefi-db-plugin.h"
#include "fu-uefi-device-private.h"

static void
fu_uefi_db_external_locked_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device_db = NULL;
	g_autoptr(FuDevice) device_db_child = NULL;
	g_autoptr(FuDevice) device_kek = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_db_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);

	/* create a db device with a child */
	device_db = fu_device_new(ctx);
	device_db_child = fu_device_new(ctx);
	fu_device_set_id(device_db, "db");
	fu_device_set_id(device_db_child, "db-child");
	fu_device_add_child(device_db, device_db_child);

	/* add db device to plugin */
	fu_plugin_add_device(plugin, device_db);
	fu_plugin_runner_device_added(plugin, device_db);

	/* register a KEK device that is external */
	device_kek = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK));
	fu_device_set_plugin(device_kek, "uefi_kek");
	fu_device_add_private_flag(device_kek, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL);
	fu_plugin_runner_device_register(plugin, device_kek);

	/* db child should be locked */
	child = g_ptr_array_index(fu_device_get_children(device_db), 0);
	g_assert_true(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED));
}

static void
fu_uefi_db_external_not_locked_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device_db = NULL;
	g_autoptr(FuDevice) device_db_child = NULL;
	g_autoptr(FuDevice) device_kek = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_db_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);

	/* create a db device with a child */
	device_db = fu_device_new(ctx);
	device_db_child = fu_device_new(ctx);
	fu_device_set_id(device_db, "db");
	fu_device_set_id(device_db_child, "db-child");
	fu_device_add_child(device_db, device_db_child);

	/* add db device to plugin */
	fu_plugin_add_device(plugin, device_db);
	fu_plugin_runner_device_added(plugin, device_db);

	/* register a KEK device that is NOT external */
	device_kek = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK));
	fu_device_set_plugin(device_kek, "uefi_kek");
	fu_plugin_runner_device_register(plugin, device_kek);

	/* db child should NOT be locked */
	child = g_ptr_array_index(fu_device_get_children(device_db), 0);
	g_assert_false(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED));
}

static void
fu_uefi_db_external_locked_device_added_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device_db = NULL;
	g_autoptr(FuDevice) device_db_child = NULL;
	g_autoptr(FuDevice) device_kek = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_db_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);

	/* register KEK device as external first */
	device_kek = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK));
	fu_device_set_plugin(device_kek, "uefi_kek");
	fu_device_add_private_flag(device_kek, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL);
	fu_plugin_runner_device_register(plugin, device_kek);

	/* then add a db device with a child */
	device_db = fu_device_new(ctx);
	device_db_child = fu_device_new(ctx);
	fu_device_set_id(device_db, "db");
	fu_device_set_id(device_db_child, "db-child");
	fu_device_add_child(device_db, device_db_child);

	/* add db device to plugin -- child should be locked via device_added */
	fu_plugin_add_device(plugin, device_db);
	fu_plugin_runner_device_added(plugin, device_db);

	child = g_ptr_array_index(fu_device_get_children(device_db), 0);
	g_assert_true(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED));
}

static FuFirmware *
fu_uefi_db_self_test_build_siglist(const gchar *subject, GError **error)
{
	g_autoptr(FuEfiX509Signature) sig = fu_efi_x509_signature_new();
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();

	fu_efi_x509_signature_set_subject(sig, subject);
	if (!fu_firmware_add_image(siglist, FU_FIRMWARE(sig), error))
		return NULL;
	return g_steal_pointer(&siglist);
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
	g_autoptr(FuFirmware) siglist_db = NULL;
	g_autoptr(FuFirmware) siglist_default = NULL;
	g_autoptr(GBytes) blob_default = NULL;
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

	/* build signature lists */
	siglist_db = fu_uefi_db_self_test_build_siglist("O=Test,CN=Test UEFI CA 2011", &error);
	g_assert_no_error(error);
	g_assert_nonnull(siglist_db);
	siglist_default = fu_uefi_db_self_test_build_siglist("O=Test,CN=Test UEFI CA 2023", &error);
	g_assert_no_error(error);
	g_assert_nonnull(siglist_default);

	/* create the device */
	device = FU_DEVICE(fu_uefi_db_device_new(ctx));

	/* set the db EFI variable */
	ret = fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
						 siglist_db,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set the dbDefault EFI variable */
	blob_default = fu_firmware_write(siglist_default, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_default);
	ret = fu_efivars_set_data_bytes(fu_context_get_efivars(ctx),
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
	g_autoptr(FuFirmware) siglist_db = NULL;
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
	siglist_db = fu_uefi_db_self_test_build_siglist("O=Test,CN=Test UEFI CA 2011", &error);
	g_assert_no_error(error);
	g_assert_nonnull(siglist_db);

	/* create the device */
	device = FU_DEVICE(fu_uefi_db_device_new(ctx));

	/* set the db EFI variable */
	ret = fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
						 siglist_db,
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

static void
fu_uefi_db_revoked_func(void)
{
	gboolean ret;
	GPtrArray *children;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) siglist_db = fu_efi_signature_list_new();
	g_autoptr(FuFirmware) siglist_dbx = fu_efi_signature_list_new();
	g_autoptr(FuFirmware) siglist_revoked = NULL;
	g_autoptr(FuFirmware) siglist_trusted = NULL;
	g_autoptr(GByteArray) buf_db = g_byte_array_new();
	g_autoptr(GBytes) blob_db = NULL;
	g_autoptr(GBytes) blob_revoked = NULL;
	g_autoptr(GBytes) blob_trusted = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_GNUTLS
	g_test_skip("requires GnuTLS");
	return;
#endif

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the db has two certificates, and the dbx has the exact same bytes for one of them */
	siglist_revoked =
	    fu_uefi_db_self_test_build_siglist("O=Test,CN=Test Revoked CA 2011", &error);
	g_assert_no_error(error);
	g_assert_nonnull(siglist_revoked);
	siglist_trusted =
	    fu_uefi_db_self_test_build_siglist("O=Test,CN=Test Trusted CA 2023", &error);
	g_assert_no_error(error);
	g_assert_nonnull(siglist_trusted);

	/* the db is two EFI_SIGNATURE_LISTs back-to-back, and writing generates a new
	 * certificate each time so the dbx has to use the same blob */
	blob_revoked = fu_firmware_write(siglist_revoked, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_revoked);
	blob_trusted = fu_firmware_write(siglist_trusted, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_trusted);
	fu_byte_array_append_bytes(buf_db, blob_revoked);
	fu_byte_array_append_bytes(buf_db, blob_trusted);
	blob_db = g_bytes_new(buf_db->data, buf_db->len);
	ret = fu_firmware_parse_bytes(siglist_db,
				      blob_db,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_firmware_parse_bytes(siglist_dbx,
				      blob_revoked,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create the device */
	device = FU_DEVICE(fu_uefi_db_device_new(ctx));
	ret = fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
						 siglist_db,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
						 siglist_dbx,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* only the trusted certificate is shown */
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, ==, 1);
	g_assert_cmpstr(fu_device_get_name(g_ptr_array_index(children, 0)), ==, "Trusted CA");
}

static void
fu_uefi_db_revoked_same_key_func(void)
{
	gboolean ret;
	GPtrArray *children;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS | FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) siglist_db = fu_efi_signature_list_new();
	g_autoptr(FuFirmware) siglist_dbx = fu_efi_signature_list_new();
	g_autoptr(FuFirmware) siglist_tmp = NULL;
	g_autoptr(FuFirmware) sig_db = NULL;
	g_autoptr(FuFirmware) sig_dbx = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GByteArray) buf_dbx = NULL;
	g_autoptr(GBytes) blob_db = NULL;
	g_autoptr(GBytes) blob_dbx = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_GNUTLS
	g_test_skip("requires GnuTLS");
	return;
#endif

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the dbx has a different certificate with the same key, made by changing the last
	 * byte of the DER data, which is inside the signature */
	siglist_tmp = fu_uefi_db_self_test_build_siglist("O=Test,CN=Test Reissued CA 2011", &error);
	g_assert_no_error(error);
	g_assert_nonnull(siglist_tmp);
	blob_db = fu_firmware_write(siglist_tmp, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_db);
	buf_dbx = g_byte_array_new();
	fu_byte_array_append_bytes(buf_dbx, blob_db);
	buf_dbx->data[buf_dbx->len - 1] ^= 0xFF;
	blob_dbx = g_bytes_new(buf_dbx->data, buf_dbx->len);

	/* sanity check the key IDs match but the data does not */
	ret = fu_firmware_parse_bytes(siglist_db,
				      blob_db,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_parse_bytes(siglist_dbx,
				      blob_dbx,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	sig_db = fu_firmware_get_image_by_idx(siglist_db, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(sig_db);
	sig_dbx = fu_firmware_get_image_by_idx(siglist_dbx, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(sig_dbx);
	g_assert_cmpstr(fu_firmware_get_id(sig_db), ==, fu_firmware_get_id(sig_dbx));
	g_assert_false(g_bytes_equal(blob_db, blob_dbx));

	/* create the device */
	device = FU_DEVICE(fu_uefi_db_device_new(ctx));
	ret = fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
						 siglist_db,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
						 siglist_dbx,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the db certificate is not blocked */
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, ==, 1);
	g_assert_cmpstr(fu_device_get_name(g_ptr_array_index(children, 0)), ==, "Reissued CA");
}

static void
fu_uefi_db_modify_config_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_db_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);
	fwupd_plugin_add_flag(FWUPD_PLUGIN(plugin), FWUPD_PLUGIN_FLAG_TEST_ONLY);

	/* valid key */
	ret = fu_plugin_runner_modify_config(plugin, "UpdateWindowsCA", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* invalid key */
	ret = fu_plugin_runner_modify_config(plugin, "NotAKey", "value", &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_uefi_db_windows_ca_lower_priority_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device_child = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_db_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);
	device = g_object_new(FU_TYPE_DEVICE, "context", ctx, NULL);
	device_child = g_object_new(FU_TYPE_DEVICE, "context", ctx, NULL);

	/* add child with Windows Production PCA instance ID */
	fu_device_set_id(device, "parent");
	fu_device_add_instance_id_full(device_child,
				       "UEFI\\CRT_1A8B6903D64CC9AD09D12FCB355663A458A09EF0",
				       FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_set_id(device_child, "child");
	fu_device_add_child(device, device_child);

	/* without dual-boot and without UpdateWindowsCA → lower priority */
	fu_plugin_runner_device_added(plugin, device);
	child = g_ptr_array_index(fu_device_get_children(device), 0);
	g_assert_true(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY));
}

static void
fu_uefi_db_windows_ca_dual_boot_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device_child = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_db_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);
	device = g_object_new(FU_TYPE_DEVICE, "context", ctx, NULL);
	device_child = g_object_new(FU_TYPE_DEVICE, "context", ctx, NULL);

	/* add child with Windows Production PCA instance ID */
	fu_device_set_id(device, "parent");
	fu_device_add_instance_id_full(device_child,
				       "UEFI\\CRT_1A8B6903D64CC9AD09D12FCB355663A458A09EF0",
				       FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_set_id(device_child, "child");
	fu_device_add_child(device, device_child);

	/* with dual-boot Windows → NOT lower priority */
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_DUAL_BOOT_WINDOWS);
	fu_plugin_runner_device_added(plugin, device);
	child = g_ptr_array_index(fu_device_get_children(device), 0);
	g_assert_false(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY));
}

static void
fu_uefi_db_windows_ca_config_override_func(void)
{
	gboolean ret;
	FuDevice *child;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device_child = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_uefi_db_plugin_get_type(), ctx);
	fu_plugin_runner_init(plugin);
	device = g_object_new(FU_TYPE_DEVICE, "context", ctx, NULL);
	device_child = g_object_new(FU_TYPE_DEVICE, "context", ctx, NULL);

	/* add child with Windows Production PCA instance ID */
	fu_device_set_id(device, "parent");
	fu_device_add_instance_id_full(device_child,
				       "UEFI\\CRT_1A8B6903D64CC9AD09D12FCB355663A458A09EF0",
				       FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_set_id(device_child, "child");
	fu_device_add_child(device, device_child);

	/* with UpdateWindowsCA=true → NOT lower priority */
	fwupd_plugin_add_flag(FWUPD_PLUGIN(plugin), FWUPD_PLUGIN_FLAG_TEST_ONLY);
	ret = fu_plugin_runner_modify_config(plugin, "UpdateWindowsCA", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_plugin_runner_device_added(plugin, device);
	child = g_ptr_array_index(fu_device_get_children(device), 0);
	g_assert_false(fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY));
}

static void
fu_uefi_db_security_attr_production_test_cert_func(void)
{
	FuDevice *child;
	GPtrArray *children;
	gboolean ret;
	gboolean seen_insecure_platform = FALSE;
	g_autofree gchar *quirks_dir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuFirmware) siglist_db = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_GNUTLS
	g_test_skip("requires GnuTLS");
	return;
#endif

	quirks_dir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, quirks_dir);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* build a db with the default MakeCert.exe test key name */
	siglist_db =
	    fu_uefi_db_self_test_build_siglist("O=Test,CN=Joe's-Software-Emporium", &error);
	g_assert_no_error(error);
	g_assert_nonnull(siglist_db);

	/* create the device */
	device = FU_DEVICE(fu_uefi_db_device_new(ctx));
	fu_device_set_plugin(device, "uefi_db");

	/* set the db EFI variable */
	ret = fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
						 siglist_db,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* probe should create a child with the insecure-platform problem from the quirk */
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, >, 0);
	for (guint i = 0; i < children->len; i++) {
		child = g_ptr_array_index(children, i);
		if (fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM)) {
			seen_insecure_platform = TRUE;
			break;
		}
	}
	g_assert_true(seen_insecure_platform);

	/* the production attr should be not-valid as a test key is enrolled */
	fu_device_add_security_attrs(device, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_UEFI_DB_PRODUCTION,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_uefi_db_security_attr_production_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device_child = NULL;
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	/* create a db device with a child that has no test key */
	device = FU_DEVICE(fu_uefi_db_device_new(ctx));
	device_child = fu_device_new(ctx);
	fu_device_set_id(device, "db");
	fu_device_set_plugin(device, "uefi_db");
	fu_device_set_id(device_child, "db-child");
	fu_device_add_child(device, device_child);

	/* run the HSI checks */
	fu_device_add_security_attrs(device, attrs);

	/* the production attr should exist and be valid */
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_UEFI_DB_PRODUCTION,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr), ==, FWUPD_SECURITY_ATTR_RESULT_VALID);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_uefi_db_security_attr_production_test_key_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device_child = NULL;
	g_autoptr(FuSecurityAttr) attr = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	/* create a db device with a child that is a test key */
	device = FU_DEVICE(fu_uefi_db_device_new(ctx));
	device_child = fu_device_new(ctx);
	fu_device_set_id(device, "db");
	fu_device_set_plugin(device, "uefi_db");
	fu_device_set_id(device_child, "db-child");
	fu_device_add_problem(device_child, FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM);
	fu_device_add_child(device, device_child);

	/* run the HSI checks */
	fu_device_add_security_attrs(device, attrs);

	/* the production attr should exist and be not-valid */
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_UEFI_DB_PRODUCTION,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/uefi-db/default-ids", fu_uefi_db_default_ids_func);
	g_test_add_func("/uefi-db/no-default-ids", fu_uefi_db_no_default_ids_func);
	g_test_add_func("/uefi-db/revoked", fu_uefi_db_revoked_func);
	g_test_add_func("/uefi-db/revoked-same-key", fu_uefi_db_revoked_same_key_func);
	g_test_add_func("/uefi-db/external-locked", fu_uefi_db_external_locked_func);
	g_test_add_func("/uefi-db/external-not-locked", fu_uefi_db_external_not_locked_func);
	g_test_add_func("/uefi-db/external-locked-device-added",
			fu_uefi_db_external_locked_device_added_func);
	g_test_add_func("/uefi-db/modify-config", fu_uefi_db_modify_config_func);
	g_test_add_func("/uefi-db/windows-ca-lower-priority",
			fu_uefi_db_windows_ca_lower_priority_func);
	g_test_add_func("/uefi-db/windows-ca-dual-boot", fu_uefi_db_windows_ca_dual_boot_func);
	g_test_add_func("/uefi-db/windows-ca-config-override",
			fu_uefi_db_windows_ca_config_override_func);
	g_test_add_func("/uefi-db/security-attr-production",
			fu_uefi_db_security_attr_production_func);
	g_test_add_func("/uefi-db/security-attr-production-test-cert",
			fu_uefi_db_security_attr_production_test_cert_func);
	g_test_add_func("/uefi-db/security-attr-production-test-key",
			fu_uefi_db_security_attr_production_test_key_func);
	return g_test_run();
}
