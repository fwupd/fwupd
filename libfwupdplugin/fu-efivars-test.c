/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-dummy-efivars.h"
#include "fu-efi-signature-private.h"
#include "fu-efivars-private.h"
#include "fu-volume-private.h"

static void
fu_efivars_func(void)
{
	gboolean ret;
	gsize sz = 0;
	guint32 attr = 0;
	guint64 total;
	guint8 buf[] = {};
	g_autofree guint8 *data = NULL;
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) names = NULL;

	/* check supported */
	ret = fu_efivars_supported(efivars, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check free space */
	total = fu_efivars_space_free(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, ==, 10240);

	/* writing an empty payload is not allowed */
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test",
				  buf,
				  sizeof(buf),
				  0,
				  &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
	g_clear_error(&error);
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test"));

	/* write and read a key */
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test",
				  (guint8 *)"1",
				  1,
				  FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				      FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test",
				  &data,
				  &sz,
				  &attr,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sz, ==, 1);
	g_assert_cmpint(attr,
			==,
			FU_EFI_VARIABLE_ATTR_NON_VOLATILE | FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS);
	g_assert_cmpint(data[0], ==, '1');

	/* check free space again */
	total = fu_efivars_space_free(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, ==, 10203);

	/* check existing keys */
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "NotGoingToExist"));
	g_assert_true(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test"));

	/* list a few keys */
	names = fu_efivars_get_names(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(names);
	g_assert_cmpint(names->len, ==, 1);

	/* check we can get the space used */
	total = fu_efivars_space_used(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, >=, 0x10);

	/* delete single key */
	ret = fu_efivars_delete(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test"));
	g_assert_false(fu_efivars_delete(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test", NULL));

	/* delete multiple keys */
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test1",
				  (guint8 *)"1",
				  1,
				  0,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "Test2",
				  (guint8 *)"1",
				  1,
				  0,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_delete_with_glob(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test*", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test1"));
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Test2"));

	/* check free space again */
	total = fu_efivars_space_free(efivars, &error);
	g_assert_no_error(error);
	g_assert_cmpint(total, ==, 10240);

	/* read a key that doesn't exist */
	ret = fu_efivars_get_data(efivars,
				  FU_EFIVARS_GUID_EFI_GLOBAL,
				  "NotGoingToExist",
				  NULL,
				  NULL,
				  NULL,
				  &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_efivars_secure_boot_func(void)
{
	struct {
		guint8 secure_boot;
		guint8 setup_mode;
		FuEfiSecureBootState state;
	} tests[] = {
	    {0, 0, FU_EFI_SECURE_BOOT_STATE_DISABLED},
	    {1, 0, FU_EFI_SECURE_BOOT_STATE_ENABLED},
	    {0, 1, FU_EFI_SECURE_BOOT_STATE_IN_SETUP},
	    {1, 1, FU_EFI_SECURE_BOOT_STATE_ENABLED | FU_EFI_SECURE_BOOT_STATE_IN_SETUP},
	};

	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		gboolean ret;
		FuEfiSecureBootState state = FU_EFI_SECURE_BOOT_STATE_DISABLED;
		g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
		g_autoptr(GError) error = NULL;

		ret = fu_efivars_set_data(efivars,
					  FU_EFIVARS_GUID_EFI_GLOBAL,
					  "SecureBoot",
					  &tests[i].secure_boot,
					  sizeof(tests[i].secure_boot),
					  0,
					  &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		ret = fu_efivars_set_data(efivars,
					  FU_EFIVARS_GUID_EFI_GLOBAL,
					  "SetupMode",
					  &tests[i].setup_mode,
					  sizeof(tests[i].setup_mode),
					  0,
					  &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		ret = fu_efivars_get_secure_boot(efivars, &state, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_assert_cmpint(state, ==, tests[i].state);
	}
}

static void
fu_efivars_boot_func(void)
{
	FuFirmware *firmware_tmp;
	gboolean ret;
	guint16 idx = 0;
	g_autofree gchar *pefile_fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuEfiLoadOption) loadopt2 = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GArray) bootorder2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) entries = NULL;
	g_autoptr(GPtrArray) esp_files = NULL;
	FuEfivars *efivars = fu_context_get_efivars(ctx);

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("efivar-boot", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	volume = fu_volume_new_from_mount_path(fu_temporary_directory_get_path(tmpdir));

	/* set and get BootCurrent */
	ret = fu_efivars_set_boot_current(efivars, 0x0001, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_boot_current(efivars, &idx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(idx, ==, 0x0001);

	/* set and get BootNext */
	ret = fu_efivars_set_boot_next(efivars, 0x0002, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_get_boot_next(efivars, &idx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(idx, ==, 0x0002);

	/* set and get BootOrder */
	ret = fu_efivars_build_boot_order(efivars, &error, 0x0001, 0x0002, G_MAXUINT16);
	g_assert_no_error(error);
	g_assert_true(ret);
	bootorder2 = fu_efivars_get_boot_order(efivars, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bootorder2);
	g_assert_cmpint(bootorder2->len, ==, 2);
	idx = g_array_index(bootorder2, guint16, 0);
	g_assert_cmpint(idx, ==, 0x0001);
	idx = g_array_index(bootorder2, guint16, 1);
	g_assert_cmpint(idx, ==, 0x0002);

	/* add a plausible ESP */
	fu_volume_set_partition_kind(volume, FU_VOLUME_KIND_ESP);
	fu_volume_set_partition_uuid(volume, "41f5e9b7-eb4f-5c65-b8a6-f94b0ad54815");
	fu_context_add_esp_volume(ctx, volume);

	/* create Boot0001 and Boot0002 */
	ret = fu_efivars_create_boot_entry_for_volume(efivars,
						      0x0001,
						      volume,
						      "Fedora",
						      "grubx64.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_create_boot_entry_for_volume(efivars,
						      0x0002,
						      volume,
						      "Firmware Update",
						      "fwupdx64.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check BootXXXX exists */
	loadopt2 = fu_efivars_get_boot_entry(efivars, 0x0001, &error);
	g_assert_no_error(error);
	g_assert_nonnull(loadopt2);
	entries = fu_efivars_get_boot_entries(efivars, &error);
	g_assert_no_error(error);
	g_assert_nonnull(entries);
	g_assert_nonnull(bootorder2);
	g_assert_cmpint(bootorder2->len, ==, 2);

	/* check we detected something */
	esp_files =
	    fu_context_get_esp_files(ctx, FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(esp_files);
	g_assert_cmpint(esp_files->len, ==, 2);
	firmware_tmp = g_ptr_array_index(esp_files, 0);
	pefile_fn = fu_temporary_directory_build(tmpdir, "grubx64.efi", NULL);
	g_assert_cmpstr(fu_firmware_get_filename(firmware_tmp), ==, pefile_fn);
}

static FuFirmware *
fu_efivars_test_siglist_new(guint n_sigs)
{
	gboolean ret;
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();
	g_autoptr(GError) error = NULL;

	for (guint i = 0; i < n_sigs; i++) {
		g_autoptr(FuEfiSignature) sig = fu_efi_signature_new(FU_EFI_SIGNATURE_KIND_SHA256);
		g_autoptr(GBytes) csum = NULL;
		g_autofree gchar *csum_str = g_strdup_printf("%064x", i);

		csum = fu_bytes_from_string(csum_str, &error);
		g_assert_no_error(error);
		g_assert_nonnull(csum);
		fu_firmware_set_bytes(FU_FIRMWARE(sig), csum);
		ret = fu_firmware_add_image(siglist, FU_FIRMWARE(sig), &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}
	return g_steal_pointer(&siglist);
}

static void
fu_efivars_secure_boot_siglist_missing_func(void)
{
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();

	for (guint kind = FU_EFI_SECURE_BOOT_SIGLIST_KIND_PK;
	     kind <= FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX;
	     kind++) {
		g_autoptr(FuFirmware) firmware = NULL;
		g_autoptr(GError) error = NULL;
		g_autoptr(GPtrArray) images = NULL;

		/* missing variable returns an empty siglist */
		firmware = fu_efivars_get_secure_boot_siglist(efivars, kind, &error);
		g_assert_no_error(error);
		g_assert_nonnull(firmware);
		g_assert_true(FU_IS_EFI_SIGNATURE_LIST(firmware));
		images = fu_firmware_get_images(firmware);
		g_assert_cmpint(images->len, ==, 0);
	}
}

static void
fu_efivars_secure_boot_siglist_func(void)
{
	struct {
		FuEfiSecureBootSiglistKind kind;
		const gchar *guid;
		const gchar *name;
		guint n_sigs;
	} tests[] = {
	    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_PK, FU_EFIVARS_GUID_EFI_GLOBAL, "PK", 1},
	    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK, FU_EFIVARS_GUID_EFI_GLOBAL, "KEK", 2},
	    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB_DEFAULT,
	     FU_EFIVARS_GUID_EFI_GLOBAL,
	     "dbDefault",
	     3},
	    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB, FU_EFIVARS_GUID_SECURITY_DATABASE, "db", 4},
	    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX, FU_EFIVARS_GUID_SECURITY_DATABASE, "dbx", 5},
	};
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();

	/* write each kind */
	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		gboolean ret;
		FuEfiVariableAttrs attr = FU_EFI_VARIABLE_ATTR_NONE;
		g_autoptr(FuFirmware) siglist = fu_efivars_test_siglist_new(tests[i].n_sigs);
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GError) error = NULL;

		ret = fu_efivars_set_secure_boot_siglist(efivars, tests[i].kind, siglist, &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		/* check the correct GUID, name and attributes were used */
		blob =
		    fu_efivars_get_data_bytes(efivars, tests[i].guid, tests[i].name, &attr, &error);
		g_assert_no_error(error);
		g_assert_nonnull(blob);
		g_assert_cmpint(attr,
				==,
				FU_EFI_VARIABLE_ATTR_APPEND_WRITE |
				    FU_EFI_VARIABLE_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS |
				    FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS |
				    FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				    FU_EFI_VARIABLE_ATTR_NON_VOLATILE);
	}

	/* read each kind back, checking they did not overwrite each other */
	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		g_autoptr(FuFirmware) firmware = NULL;
		g_autoptr(GError) error = NULL;
		g_autoptr(GPtrArray) images = NULL;

		firmware = fu_efivars_get_secure_boot_siglist(efivars, tests[i].kind, &error);
		g_assert_no_error(error);
		g_assert_nonnull(firmware);
		g_assert_true(FU_IS_EFI_SIGNATURE_LIST(firmware));
		images = fu_firmware_get_images(firmware);
		g_assert_cmpint(images->len, ==, tests[i].n_sigs);
	}
}

static void
fu_efivars_secure_boot_siglist_cache_func(void)
{
	gboolean ret;
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
	g_autoptr(FuFirmware) firmware1 = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;
	g_autoptr(FuFirmware) firmware3 = NULL;
	g_autoptr(FuFirmware) firmware4 = NULL;
	g_autoptr(FuFirmware) siglist1 = fu_efivars_test_siglist_new(1);
	g_autoptr(FuFirmware) siglist2 = fu_efivars_test_siglist_new(2);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images2 = NULL;
	g_autoptr(GPtrArray) images3 = NULL;
	g_autoptr(GPtrArray) images4 = NULL;

	/* populate the cache */
	ret = fu_efivars_set_secure_boot_siglist(efivars,
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
						 siglist1,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware1 =
	    fu_efivars_get_secure_boot_siglist(efivars, FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware1);

	/* change the variable behind the cache, which should be ignored */
	blob = fu_firmware_write(siglist2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ret = fu_efivars_set_data_bytes(efivars,
					FU_EFIVARS_GUID_SECURITY_DATABASE,
					"db",
					blob,
					FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware2 =
	    fu_efivars_get_secure_boot_siglist(efivars, FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB, &error);
	g_assert_no_error(error);
	g_assert_true(firmware2 == firmware1);
	images2 = fu_firmware_get_images(firmware2);
	g_assert_cmpint(images2->len, ==, 1);

	/* setting a different kind does not invalidate the cache */
	ret = fu_efivars_set_secure_boot_siglist(efivars,
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
						 siglist2,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware3 =
	    fu_efivars_get_secure_boot_siglist(efivars, FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB, &error);
	g_assert_no_error(error);
	g_assert_true(firmware3 == firmware1);
	images3 = fu_firmware_get_images(firmware3);
	g_assert_cmpint(images3->len, ==, 1);

	/* setting the same kind invalidates the cache */
	ret = fu_efivars_set_secure_boot_siglist(efivars,
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
						 siglist2,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware4 =
	    fu_efivars_get_secure_boot_siglist(efivars, FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware4);
	g_assert_true(firmware4 != firmware1);
	images4 = fu_firmware_get_images(firmware4);
	g_assert_cmpint(images4->len, ==, 2);
}

static void
fu_efivars_secure_boot_siglist_invalid_func(void)
{
	gboolean ret;
	const guint8 buf[] = {0xFF};
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;
	g_autoptr(FuFirmware) siglist = fu_efivars_test_siglist_new(1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* corrupt variable fails to parse */
	ret = fu_efivars_set_data(efivars,
				  FU_EFIVARS_GUID_SECURITY_DATABASE,
				  "dbx",
				  buf,
				  sizeof(buf),
				  FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware = fu_efivars_get_secure_boot_siglist(efivars,
						      FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
						      &error);
	g_assert_nonnull(error);
	g_assert_true(g_str_has_prefix(error->message, "failed to parse "));
	g_assert_null(firmware);
	g_clear_error(&error);

	/* failure was not cached, so fixing the variable works */
	ret = fu_efivars_set_secure_boot_siglist(efivars,
						 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
						 siglist,
						 &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware2 = fu_efivars_get_secure_boot_siglist(efivars,
						       FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
						       &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware2);
	images = fu_firmware_get_images(firmware2);
	g_assert_cmpint(images->len, ==, 1);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/efivars", fu_efivars_func);
	g_test_add_func("/fwupd/efivars/secure-boot", fu_efivars_secure_boot_func);
	g_test_add_func("/fwupd/efivars/bootxxxx", fu_efivars_boot_func);
	g_test_add_func("/fwupd/efivars/secure-boot-siglist", fu_efivars_secure_boot_siglist_func);
	g_test_add_func("/fwupd/efivars/secure-boot-siglist/missing",
			fu_efivars_secure_boot_siglist_missing_func);
	g_test_add_func("/fwupd/efivars/secure-boot-siglist/cache",
			fu_efivars_secure_boot_siglist_cache_func);
	g_test_add_func("/fwupd/efivars/secure-boot-siglist/invalid",
			fu_efivars_secure_boot_siglist_invalid_func);
	return g_test_run();
}
