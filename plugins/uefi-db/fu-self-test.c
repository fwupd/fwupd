/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-efi-x509-signature-private.h"
#include "fu-plugin-private.h"
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
	device_kek = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "KEK"));
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
	device_kek = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "KEK"));
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
	device_kek = FU_DEVICE(fu_uefi_device_new(ctx, FU_EFIVARS_GUID_EFI_GLOBAL, "KEK"));
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

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/uefi-db/default-ids", fu_uefi_db_default_ids_func);
	g_test_add_func("/uefi-db/no-default-ids", fu_uefi_db_no_default_ids_func);
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
	return g_test_run();
}
