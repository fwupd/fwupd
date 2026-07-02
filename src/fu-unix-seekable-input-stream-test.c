/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#ifdef HAVE_MEMFD_CREATE
#include <sys/mman.h>
#endif

#include "fwupd-error.h"

#include "fu-unix-seekable-input-stream.h"

static void
fu_unix_seekable_input_stream_func(void)
{
#ifdef HAVE_GIO_UNIX
	gssize ret;
	g_autofd gint fd = -1;
	guint8 buf[6] = {0};
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	g_assert_nonnull(fn);
	fd = g_open(fn, O_RDONLY, 0);
	g_assert_cmpint(fd, >=, 0);

	stream = fu_unix_seekable_input_stream_new(g_steal_fd(&fd), TRUE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);

	/* first chuck */
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, "<?xml");

	/* second chuck */
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, " vers");

	/* first chuck, again */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 1);
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, "<?xml");
#else
	g_test_skip("No gio-unix-2.0 support, skipping");
#endif
}

static void
fu_unix_seekable_input_stream_non_regular_func(void)
{
#ifdef HAVE_GIO_UNIX
	g_autofd gint fd = -1;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	fd = g_open("/dev/zero", O_RDONLY, 0);
	g_assert_cmpint(fd, >=, 0);

	stream = fu_unix_seekable_input_stream_new(g_steal_fd(&fd), TRUE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(stream);
#else
	g_test_skip("No gio-unix-2.0 support, skipping");
#endif
}

static void
fu_unix_seekable_input_stream_sealed_memfd_func(void)
{
#if defined(HAVE_GIO_UNIX) && defined(HAVE_MEMFD_CREATE)
	g_autofd gint fd = -1;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	const gchar data[] = "hello";

	fd = memfd_create("fwupd-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	g_assert_cmpint(fd, >=, 0);
	g_assert_cmpint(write(fd, data, sizeof(data)), ==, sizeof(data));
	g_assert_cmpint(lseek(fd, 0, SEEK_SET), ==, 0);
	g_assert_cmpint(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW), ==, 0);

	stream = fu_unix_seekable_input_stream_new(g_steal_fd(&fd), TRUE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
#else
	g_test_skip("No gio-unix-2.0 or memfd_create support, skipping");
#endif
}

static void
fu_unix_seekable_input_stream_tmpfs_func(void)
{
#if defined(HAVE_GIO_UNIX) && defined(HAVE_MEMFD_CREATE)
	g_autofd gint fd = -1;
	g_autofree gchar *fn = g_strdup("/dev/shm/fwupd-test-XXXXXX");
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	const gchar data[] = "hello";

	/* regular files on tmpfs also support F_GET_SEALS, reporting F_SEAL_SEAL */
	fd = g_mkstemp(fn);
	if (fd < 0) {
		g_test_skip("cannot create file on /dev/shm, skipping");
		return;
	}
	g_assert_cmpint(g_unlink(fn), ==, 0);
	g_assert_cmpint(write(fd, data, sizeof(data)), ==, sizeof(data));
	g_assert_cmpint(lseek(fd, 0, SEEK_SET), ==, 0);

	stream = fu_unix_seekable_input_stream_new(g_steal_fd(&fd), TRUE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
#else
	g_test_skip("No gio-unix-2.0 or memfd_create support, skipping");
#endif
}

static void
fu_unix_seekable_input_stream_unsealed_memfd_func(void)
{
#if defined(HAVE_GIO_UNIX) && defined(HAVE_MEMFD_CREATE)
	g_autofd gint fd = -1;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	const gchar data[] = "hello";

	fd = memfd_create("fwupd-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	g_assert_cmpint(fd, >=, 0);
	g_assert_cmpint(write(fd, data, sizeof(data)), ==, sizeof(data));
	g_assert_cmpint(lseek(fd, 0, SEEK_SET), ==, 0);

	stream = fu_unix_seekable_input_stream_new(g_steal_fd(&fd), TRUE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(stream);
#else
	g_test_skip("No gio-unix-2.0 or memfd_create support, skipping");
#endif
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/unix-seekable-input-stream", fu_unix_seekable_input_stream_func);
	g_test_add_func("/fwupd/unix-seekable-input-stream/non-regular",
			fu_unix_seekable_input_stream_non_regular_func);
	g_test_add_func("/fwupd/unix-seekable-input-stream/sealed-memfd",
			fu_unix_seekable_input_stream_sealed_memfd_func);
	g_test_add_func("/fwupd/unix-seekable-input-stream/tmpfs",
			fu_unix_seekable_input_stream_tmpfs_func);
	g_test_add_func("/fwupd/unix-seekable-input-stream/unsealed-memfd",
			fu_unix_seekable_input_stream_unsealed_memfd_func);
	return g_test_run();
}
