/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-lzma-common.h"
#include "fu-test.h"

static void
fu_lzma_func(void)
{
	gboolean ret;
	g_autoptr(GByteArray) buf_in = g_byte_array_new();
	g_autoptr(GBytes) blob_in = NULL;
	g_autoptr(GBytes) blob_orig = NULL;
	g_autoptr(GBytes) blob_out = NULL;
	g_autoptr(GError) error = NULL;

	/* create a repeating pattern */
	for (guint i = 0; i < 10000; i++) {
		guint8 tmp = i % 8;
		g_byte_array_append(buf_in, &tmp, sizeof(tmp));
	}
	blob_in = g_bytes_new(buf_in->data, buf_in->len);

	/* compress */
	blob_out = fu_lzma_compress_bytes(blob_in, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_out);
	g_assert_cmpint(g_bytes_get_size(blob_out), <, 500);

	/* decompress */
	blob_orig = fu_lzma_decompress_bytes(blob_out, 128 * 1024 * 1024, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_orig);
	ret = fu_bytes_compare(blob_in, blob_orig, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/lzma", fu_lzma_func);
	return g_test_run();
}
