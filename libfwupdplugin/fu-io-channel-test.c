/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_io_channel_virtual_func(void)
{
	gboolean ret;
	gsize bytes_read = 0;
	guint8 buf[5] = {0};
	const guint8 data[] = {'h', 'e', 'l', 'l', 'o'};
	g_autoptr(FuIOChannel) io = NULL;
	g_autoptr(GByteArray) array = NULL;
	g_autoptr(GByteArray) array_wr = g_byte_array_new();
	g_autoptr(GBytes) blob = g_bytes_new_static(data, sizeof(data));
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GError) error = NULL;

	io = fu_io_channel_virtual_new("fwupd-self-test", &error);
	if (io == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no memfd support");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(io);
	g_assert_cmpint(fu_io_channel_unix_get_fd(io), >=, 0);

	/* write some bytes, rewind, then read them back */
	ret = fu_io_channel_write_bytes(io, blob, 1000, FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_io_channel_seek(io, 0x0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob2 = fu_io_channel_read_bytes(io,
					 sizeof(data),
					 1000,
					 FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					 &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_true(fu_bytes_compare(blob, blob2, &error));

	/* raw write and read, via a byte array */
	ret = fu_io_channel_seek(io, 0x0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_byte_array_append_uint8(array_wr, 0x12);
	fu_byte_array_append_uint8(array_wr, 0x34);
	ret = fu_io_channel_write_byte_array(io,
					     array_wr,
					     1000,
					     FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_io_channel_seek(io, 0x0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_io_channel_read_raw(io,
				     buf,
				     0x2,
				     &bytes_read,
				     1000,
				     FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(bytes_read, ==, 2);
	g_assert_cmpint(buf[0], ==, 0x12);
	g_assert_cmpint(buf[1], ==, 0x34);

	/* shutdown closes the fd */
	ret = fu_io_channel_shutdown(io, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_io_channel_file_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuIOChannel) io = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GByteArray) array = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 data[] = {0xDE, 0xAD, 0xBE, 0xEF};

	/* seed a file on disk */
	tmpdir = fu_temporary_directory_new("fu-io-channel-self-test", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fn = fu_temporary_directory_build(tmpdir, "io.bin", NULL);
	ret = g_file_set_contents(fn, (const gchar *)data, sizeof(data), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* open it read-only and read the whole thing back */
	io = fu_io_channel_new_file(fn, FU_IO_CHANNEL_OPEN_FLAG_READ, &error);
	g_assert_no_error(error);
	g_assert_nonnull(io);
	array =
	    fu_io_channel_read_byte_array(io, -1, 1000, FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO, &error);
	g_assert_no_error(error);
	g_assert_nonnull(array);
	g_assert_cmpint(array->len, ==, sizeof(data));
	g_assert_cmpmem(array->data, array->len, data, sizeof(data));
}

static void
fu_io_channel_file_invalid_func(void)
{
	g_autoptr(FuIOChannel) io = NULL;
	g_autoptr(GError) error = NULL;

	io = fu_io_channel_new_file("/this/does/not/exist.bin",
				    FU_IO_CHANNEL_OPEN_FLAG_READ,
				    &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(io);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/io-channel/virtual", fu_io_channel_virtual_func);
	g_test_add_func("/fwupd/io-channel/file", fu_io_channel_file_func);
	g_test_add_func("/fwupd/io-channel/file-invalid", fu_io_channel_file_invalid_func);
	return g_test_run();
}
