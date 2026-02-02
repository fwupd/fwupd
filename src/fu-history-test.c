/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-history.h"

static void
fu_history_func(void)
{
	GPtrArray *checksums;
	gboolean ret;
	FuDevice *device;
	FuRelease *release;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device_found = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) approved_firmware = NULL;
	const gchar *dirname;
	g_autofree gchar *filename = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("history", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* create */
	history = fu_history_new(ctx);
	g_assert_nonnull(history);

	/* delete the database */
	dirname = fu_context_get_path(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dirname);
	if (!g_file_test(dirname, G_FILE_TEST_IS_DIR))
		return;

	/* add a device */
	device = fu_device_new(ctx);
	fu_device_set_id(device, "self-test");
	fu_device_set_name(device, "ColorHug"),
	    fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "3.0.1"),
	    fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
	fu_device_set_update_error(device, "word");
	fu_device_add_instance_id(device, "827edddd-9bb6-5632-889f-2c01255503da");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_created_usec(device, 1514338000ull * G_USEC_PER_SEC);
	fu_device_set_modified_usec(device, 1514338999ull * G_USEC_PER_SEC);
	release = fu_release_new();
	fu_release_set_filename(release, "/var/lib/dave.cap"),
	    fu_release_add_checksum(release, "abcdef");
	fu_release_set_version(release, "3.0.2");
	fu_release_add_metadata_item(release, "FwupdVersion", VERSION);
	ret = fu_history_add_device(history, device, release, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_object_unref(release);

	/* ensure database was created */
	filename = g_build_filename(dirname, "pending.db", NULL);
	g_assert_true(g_file_test(filename, G_FILE_TEST_EXISTS));

	g_object_unref(device);

	/* get device */
	device = fu_history_get_device_by_id(history,
					     "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "2ba16d10df45823dd4494ff10a0bfccfef512c9d");
	g_assert_cmpstr(fu_device_get_name(device), ==, "ColorHug");
	g_assert_cmpstr(fu_device_get_version(device), ==, "3.0.1");
	g_assert_cmpint(fu_device_get_update_state(device), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr(fu_device_get_update_error(device), ==, "word");
	g_assert_cmpstr(fu_device_get_guid_default(device),
			==,
			"827edddd-9bb6-5632-889f-2c01255503da");
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_INTERNAL | FWUPD_DEVICE_FLAG_HISTORICAL);
	g_assert_cmpint(fu_device_get_created_usec(device), ==, 1514338000ull * G_USEC_PER_SEC);
	g_assert_cmpint(fu_device_get_modified_usec(device), ==, 1514338999ull * G_USEC_PER_SEC);
	release = FU_RELEASE(fu_device_get_release_default(device));
	g_assert_nonnull(release);
	g_assert_cmpstr(fu_release_get_version(release), ==, "3.0.2");
	g_assert_cmpstr(fu_release_get_filename(release), ==, "/var/lib/dave.cap");
	g_assert_cmpstr(fu_release_get_metadata_item(release, "FwupdVersion"), ==, VERSION);
	checksums = fu_release_get_checksums(release);
	g_assert_nonnull(checksums);
	g_assert_cmpint(checksums->len, ==, 1);
	g_assert_cmpstr(fwupd_checksum_get_by_kind(checksums, G_CHECKSUM_SHA1), ==, "abcdef");
	ret = fu_history_add_device(history, device, release, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get device that does not exist */
	device_found = fu_history_get_device_by_id(history, "XXXXXXXXXXXXX", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device_found);
	g_clear_error(&error);

	/* get device that does exist */
	device_found = fu_history_get_device_by_id(history,
						   "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
						   &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_found);
	g_object_unref(device_found);

	/* remove device */
	ret = fu_history_remove_device(history, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_object_unref(device);

	/* get device that does not exist */
	device_found = fu_history_get_device_by_id(history,
						   "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
						   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device_found);
	g_clear_error(&error);

	/* approved firmware */
	ret = fu_history_clear_approved_firmware(history, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_add_approved_firmware(history, "foo", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_add_approved_firmware(history, "bar", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	approved_firmware = fu_history_get_approved_firmware(history, &error);
	g_assert_no_error(error);
	g_assert_nonnull(approved_firmware);
	g_assert_cmpint(approved_firmware->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(approved_firmware, 0), ==, "foo");
	g_assert_cmpstr(g_ptr_array_index(approved_firmware, 1), ==, "bar");

	/* emulation-tag */
	ret = fu_history_add_emulation_tag(history, "id", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_has_emulation_tag(history, "id", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_has_emulation_tag(history, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_remove_emulation_tag(history, "id", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_remove_emulation_tag(history, "id", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_has_emulation_tag(history, "id", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_history_modify_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuHistory) history = fu_history_new(ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("engine-history-modify", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* add a new entry */
	fu_device_set_id(device, "foobarbaz");
	fu_history_remove_device(history, device, NULL);
	ret = fu_history_add_device(history, device, release, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* try to modify something that does exist */
	ret = fu_history_modify_device(history, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* does not exist */
	fu_device_set_id(device, "DOES-NOT-EXIST");
	ret = fu_history_modify_device(history, device, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
}

static void
fu_history_migrate_v1_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_dst = NULL;
	g_autoptr(GFile) file_src = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *history_fn = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("migrate-v1", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);
	history_fn = fu_context_build_filename(ctx,
					       &error,
					       FU_PATH_KIND_LOCALSTATEDIR_PKG,
					       "pending.db",
					       NULL);
	g_assert_no_error(error);
	g_assert_nonnull(history_fn);

	/* load old version */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "history_v1.db", NULL);
	file_src = g_file_new_for_path(filename);
	file_dst = g_file_new_for_path(history_fn);
	ret = g_file_copy(file_src, file_dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create, migrating as required */
	history = fu_history_new(ctx);
	g_assert_nonnull(history);

	/* get device */
	device = fu_history_get_device_by_id(history,
					     "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "2ba16d10df45823dd4494ff10a0bfccfef512c9d");
}

static void
fu_history_migrate_v2_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_dst = NULL;
	g_autoptr(GFile) file_src = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *history_fn = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("migrate-v2", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);
	history_fn = fu_context_build_filename(ctx,
					       &error,
					       FU_PATH_KIND_LOCALSTATEDIR_PKG,
					       "pending.db",
					       NULL);
	g_assert_no_error(error);
	g_assert_nonnull(history_fn);

	/* load old version */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "history_v2.db", NULL);
	file_src = g_file_new_for_path(filename);
	file_dst = g_file_new_for_path(history_fn);
	ret = g_file_copy(file_src, file_dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create, migrating as required */
	history = fu_history_new(ctx);
	g_assert_nonnull(history);

	/* get device */
	device = fu_history_get_device_by_id(history,
					     "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "2ba16d10df45823dd4494ff10a0bfccfef512c9d");
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/history", fu_history_func);
	g_test_add_func("/fwupd/history/modify", fu_history_modify_func);
	g_test_add_func("/fwupd/history/migrate-v1", fu_history_migrate_v1_func);
	g_test_add_func("/fwupd/history/migrate-v2", fu_history_migrate_v2_func);
	return g_test_run();
}
