/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-dummy-efivars.h"
#include "fu-efivars-private.h"
#include "fu-volume-private.h"

static void
fu_efivars_func(void)
{
	gboolean ret;
	gsize sz = 0;
	guint32 attr = 0;
	guint64 total;
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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/efivars", fu_efivars_func);
	g_test_add_func("/fwupd/efivars/bootxxxx", fu_efivars_boot_func);
	return g_test_run();
}
