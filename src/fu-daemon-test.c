/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-remote-private.h"

#include "fu-context-private.h"
#include "fu-daemon.h"
#include "fu-remote.h"
#include "fu-test.h"

static void
fu_daemon_filename_hint_symlink_func(void)
{
	gboolean ret;
	g_autofree gchar *fn_link = NULL;
	g_autofree gchar *fn_target = NULL;
	g_autoptr(FuDaemon) daemon = g_object_new(FU_TYPE_DAEMON, NULL);
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GFile) file_link = NULL;
	g_autoptr(GError) error = NULL;

	/* create a symlink pointing at a real file */
	tmpdir = fu_temporary_directory_new("fwupd-filename-hint", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fn_target = fu_temporary_directory_build(tmpdir, "target.cab", NULL);
	g_file_set_contents(fn_target, "hello", -1, &error);
	g_assert_no_error(error);
	fn_link = fu_temporary_directory_build(tmpdir, "link.cab", NULL);
	file_link = g_file_new_for_path(fn_link);
	ret = g_file_make_symbolic_link(file_link, fn_target, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* reading from a symlink is not allowed */
	stream = fu_daemon_input_stream_from_filename_hint(daemon, fn_link, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(stream);
}

static void
fu_daemon_save_remote_directory(FuTemporaryDirectory *tmpdir)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	uri = g_strdup_printf("file://%s", fu_temporary_directory_get_path(tmpdir));
	fwupd_remote_set_id(remote, "directory");
	fwupd_remote_set_metadata_uri(remote, uri);
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ENABLED);

	fn = fu_temporary_directory_build(tmpdir, "remotes.d", "directory.conf", NULL);
	ret = fu_remote_save_to_filename(remote, fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_daemon_filename_hint_directory_func(void)
{
	gboolean ret;
	g_autofree gchar *fn_archive = NULL;
	g_autoptr(FuDaemon) daemon = g_object_new(FU_TYPE_DAEMON, NULL);
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(daemon);
	FuContext *ctx = fu_engine_get_context(engine);

	/* set up an enabled directory remote pointing at the tmpdir */
	tmpdir = fu_temporary_directory_new("fwupd-filename-hint", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_CACHEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_DATADIR_PKG, tmpdir);
	fu_daemon_save_remote_directory(tmpdir);

	/* load the engine so the remote is known */
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* drop an archive into the remote directory and read it via the hint */
	fn_archive = fu_temporary_directory_build(tmpdir, "foo.cab", NULL);
	g_file_set_contents(fn_archive, "hello", -1, &error);
	g_assert_no_error(error);
	stream = fu_daemon_input_stream_from_filename_hint(daemon, fn_archive, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
}

static void
fu_daemon_codec_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuDaemon) daemon = g_object_new(FU_TYPE_DAEMON, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	FuEngine *engine = fu_daemon_get_engine(daemon);
	FuContext *ctx = fu_engine_get_context(engine);

	/* load dummy hwids */
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* dump */
	str = fwupd_codec_to_string(FWUPD_CODEC(daemon));
	g_debug("%s", str);
	g_assert_nonnull(str);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("FWUPD_SELF_TEST", "1", TRUE);
	g_test_add_func("/fwupd/daemon/codec", fu_daemon_codec_func);
	g_test_add_func("/fwupd/daemon/filename-hint{symlink}",
			fu_daemon_filename_hint_symlink_func);
	g_test_add_func("/fwupd/daemon/filename-hint{directory}",
			fu_daemon_filename_hint_directory_func);
	return g_test_run();
}
