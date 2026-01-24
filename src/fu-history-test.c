/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-history.h"
#include "fu-release.h"

static void
fu_history_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	GPtrArray *checksums;
	gboolean ret;
	FuDevice *device;
	FuRelease *release;
	g_autoptr(FuDevice) device_found = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) approved_firmware = NULL;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;

	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("history", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	(void)g_setenv("FWUPD_LOCALSTATEDIR", fu_temporary_directory_get_path(tmpdir), TRUE);

	/* create */
	history = fu_history_new(ctx);
	g_assert_nonnull(history);

	/* delete the database */
	dirname = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	if (!g_file_test(dirname, G_FILE_TEST_IS_DIR))
		return;
	filename = g_build_filename(dirname, "pending.db", NULL);
	(void)g_unlink(filename);

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
fu_history_migrate_v1_func(gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_dst = NULL;
	g_autoptr(GFile) file_src = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autofree gchar *filename_src = NULL;
	g_autofree gchar *filename_dst = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("history", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	(void)g_setenv("FWUPD_LOCALSTATEDIR", fu_temporary_directory_get_path(tmpdir), TRUE);

	/* load old version into the tmpdir */
	filename_src = g_test_build_filename(G_TEST_DIST, "tests", "history_v1.db", NULL);
	file_src = g_file_new_for_path(filename_src);
	filename_dst = fu_temporary_directory_build(tmpdir, "lib", "fwupd", "pending.db", NULL);
	ret = fu_path_mkdir_parent(filename_dst, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	file_dst = g_file_new_for_path(filename_dst);
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
fu_history_migrate_v2_func(gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_dst = NULL;
	g_autoptr(GFile) file_src = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autofree gchar *filename_src = NULL;
	g_autofree gchar *filename_dst = NULL;

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("history", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	(void)g_setenv("FWUPD_LOCALSTATEDIR", fu_temporary_directory_get_path(tmpdir), TRUE);

	/* load old version into the tmpdir */
	filename_src = g_test_build_filename(G_TEST_DIST, "tests", "history_v2.db", NULL);
	file_src = g_file_new_for_path(filename_src);
	filename_dst = fu_temporary_directory_build(tmpdir, "lib", "fwupd", "pending.db", NULL);
	ret = fu_path_mkdir_parent(filename_dst, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	file_dst = g_file_new_for_path(filename_dst);
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
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(GError) error = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);

	/* do not save silo */
	ctx = fu_context_new();
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_IDLE_SOURCES);
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_test_add_data_func("/fwupd/history", ctx, fu_history_func);
	g_test_add_data_func("/fwupd/history/migrate-v1", ctx, fu_history_migrate_v1_func);
	g_test_add_data_func("/fwupd/history/migrate-v2", ctx, fu_history_migrate_v2_func);
	return g_test_run();
}
