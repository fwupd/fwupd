/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"
#include "fu-volume-private.h"

typedef struct {
	guint cnt;
	gchar *filename;
} FuVolumeWriteFileHelper;

static void
fu_volume_write_file_helper_free(FuVolumeWriteFileHelper *helper)
{
	g_free(helper->filename);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuVolumeWriteFileHelper, fu_volume_write_file_helper_free)

static void
fu_volume_write_file_cb(FuVolume *volume, const gchar *filename, gpointer user_data)
{
	FuVolumeWriteFileHelper *helper = (FuVolumeWriteFileHelper *)user_data;
	helper->cnt++;
	g_set_str(&helper->filename, filename);
}

static void
fu_volume_write_file_func(void)
{
	gboolean ret;
	g_autofree gchar *contents = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(FuVolumeWriteFileHelper) helper = g_new0(FuVolumeWriteFileHelper, 1);
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("volume-write-file", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	volume = fu_volume_new_from_mount_path(fu_temporary_directory_get_path(tmpdir));
	g_signal_connect(FU_VOLUME(volume),
			 "write-file",
			 G_CALLBACK(fu_volume_write_file_cb),
			 helper);

	/* write a file into a not-yet-existing subdirectory */
	filename =
	    g_build_filename(fu_temporary_directory_get_path(tmpdir), "subdir", "hello.txt", NULL);
	bytes = g_bytes_new_static("hello", 5);
	ret = fu_volume_write_file(volume, filename, bytes, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the signal was emitted with the correct filename */
	g_assert_cmpint(helper->cnt, ==, 1);
	g_assert_cmpstr(helper->filename, ==, filename);

	/* the parent directory was created and the contents written */
	ret = g_file_get_contents(filename, &contents, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(contents, ==, "hello");
}

static void
fu_volume_gpt_type_func(void)
{
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0xef"),
			==,
			"c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0x0b"),
			==,
			"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("fat32lba"),
			==,
			"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
	g_assert_cmpstr(fu_volume_kind_convert_to_gpt("0x00"), ==, "0x00");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/volume/gpt-type", fu_volume_gpt_type_func);
	g_test_add_func("/fwupd/volume/write-file", fu_volume_write_file_func);
	return g_test_run();
}
