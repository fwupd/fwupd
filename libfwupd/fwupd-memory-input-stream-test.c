/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-memory-input-stream.h"

static void
fwupd_memory_input_stream_new_from_filename_func(void)
{
	gssize ret;
	guint8 buf[6] = {0};
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdMemoryInputStream) stream = NULL;
	g_autoptr(FwupdMemoryInputStream) stream_missing = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "os-release", NULL);
	stream = fwupd_memory_input_stream_new_from_filename(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret =
	    fwupd_input_stream_read(FWUPD_INPUT_STREAM(stream), buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, "NAME=");

	stream_missing = fwupd_memory_input_stream_new_from_filename("/nonexistent", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(stream_missing);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/memory-input-stream/new-from-filename",
			fwupd_memory_input_stream_new_from_filename_func);
	return g_test_run();
}
