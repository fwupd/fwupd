/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_chunk_array_func(void)
{
	g_autoptr(FuChunk) chk1 = NULL;
	g_autoptr(FuChunk) chk2 = NULL;
	g_autoptr(FuChunk) chk3 = NULL;
	g_autoptr(FuChunk) chk4 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) fw = g_bytes_new_static("hello world", 11);
	g_autoptr(FuChunkArray) chunks =
	    fu_chunk_array_new_from_bytes(fw, 100, FU_CHUNK_PAGESZ_NONE, 5, NULL);

	g_assert_cmpint(fu_chunk_array_length(chunks), ==, 3);

	chk1 = fu_chunk_array_index(chunks, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chk1);
	g_assert_cmpint(fu_chunk_get_idx(chk1), ==, 0x0);
	g_assert_cmpint(fu_chunk_get_address(chk1), ==, 100);
	g_assert_cmpint(fu_chunk_get_data_sz(chk1), ==, 0x5);
	g_assert_cmpint(strncmp((const gchar *)fu_chunk_get_data(chk1), "hello", 5), ==, 0);

	chk2 = fu_chunk_array_index(chunks, 1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chk2);
	g_assert_cmpint(fu_chunk_get_idx(chk2), ==, 0x1);
	g_assert_cmpint(fu_chunk_get_address(chk2), ==, 105);
	g_assert_cmpint(fu_chunk_get_data_sz(chk2), ==, 0x5);
	g_assert_cmpint(strncmp((const gchar *)fu_chunk_get_data(chk2), " world", 6), ==, 0);

	chk3 = fu_chunk_array_index(chunks, 2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chk3);
	g_assert_cmpint(fu_chunk_get_idx(chk3), ==, 0x2);
	g_assert_cmpint(fu_chunk_get_address(chk3), ==, 110);
	g_assert_cmpint(fu_chunk_get_data_sz(chk3), ==, 0x1);
	g_assert_cmpint(strncmp((const gchar *)fu_chunk_get_data(chk3), "d", 1), ==, 0);

	chk4 = fu_chunk_array_index(chunks, 3, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(chk4);
	chk4 = fu_chunk_array_index(chunks, 1024, NULL);
	g_assert_null(chk4);
}

static void
fu_chunk_func(void)
{
	g_autofree gchar *chunked1_str = NULL;
	g_autofree gchar *chunked2_str = NULL;
	g_autofree gchar *chunked3_str = NULL;
	g_autofree gchar *chunked4_str = NULL;
	g_autofree gchar *chunked5_str = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;
	g_autoptr(GPtrArray) chunked3 = NULL;
	g_autoptr(GPtrArray) chunked4 = NULL;
	g_autoptr(GPtrArray) chunked5 = NULL;

	chunked3 = fu_chunk_array_new((const guint8 *)"123456", 6, 0x0, 3, 3, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked3);
	chunked3_str = fu_chunk_array_to_string(chunked3);
	g_assert_cmpstr(chunked3_str,
			==,
			"[\n"
			"  {\n"
			"    \"data\": \"123\",\n"
			"    \"data_size\": 3\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 1,\n"
			"    \"page\": 1,\n"
			"    \"data\": \"456\",\n"
			"    \"data_size\": 3\n"
			"  }\n"
			"]");

	chunked4 = fu_chunk_array_new((const guint8 *)"123456", 6, 0x4, 4, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked4);
	chunked4_str = fu_chunk_array_to_string(chunked4);
	g_assert_cmpstr(chunked4_str,
			==,
			"[\n"
			"  {\n"
			"    \"page\": 1,\n"
			"    \"data\": \"1234\",\n"
			"    \"data_size\": 4\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 1,\n"
			"    \"page\": 2,\n"
			"    \"data\": \"56\",\n"
			"    \"data_size\": 2\n"
			"  }\n"
			"]");
	chunked5 = fu_chunk_array_new(NULL, 0, 0x0, 0x0, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked5);
	g_assert_cmpint(chunked5->len, ==, 0);
	chunked5_str = fu_chunk_array_to_string(chunked5);
	g_assert_cmpstr(chunked5_str, ==, "[\n]");

	chunked1 = fu_chunk_array_new((const guint8 *)"0123456789abcdef", 16, 0x0, 10, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked1);
	chunked1_str = fu_chunk_array_to_string(chunked1);
	g_assert_cmpstr(chunked1_str,
			==,
			"[\n"
			"  {\n"
			"    \"data\": \"0123\",\n"
			"    \"data_size\": 4\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 1,\n"
			"    \"addr\": 4,\n"
			"    \"data\": \"4567\",\n"
			"    \"data_size\": 4\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 2,\n"
			"    \"addr\": 8,\n"
			"    \"data\": \"89\",\n"
			"    \"data_size\": 2\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 3,\n"
			"    \"page\": 1,\n"
			"    \"data\": \"abcd\",\n"
			"    \"data_size\": 4\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 4,\n"
			"    \"page\": 1,\n"
			"    \"addr\": 4,\n"
			"    \"data\": \"ef\",\n"
			"    \"data_size\": 2\n"
			"  }\n"
			"]");
	chunked2 = fu_chunk_array_new((const guint8 *)"XXXXXXYYYYYYZZZZZZ", 18, 0x0, 6, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked2);
	chunked2_str = fu_chunk_array_to_string(chunked2);
	g_debug("%s", chunked2_str);
	g_assert_cmpstr(chunked2_str,
			==,
			"[\n"
			"  {\n"
			"    \"data\": \"XXXX\",\n"
			"    \"data_size\": 4\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 1,\n"
			"    \"addr\": 4,\n"
			"    \"data\": \"XX\",\n"
			"    \"data_size\": 2\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 2,\n"
			"    \"page\": 1,\n"
			"    \"data\": \"YYYY\",\n"
			"    \"data_size\": 4\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 3,\n"
			"    \"page\": 1,\n"
			"    \"addr\": 4,\n"
			"    \"data\": \"YY\",\n"
			"    \"data_size\": 2\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 4,\n"
			"    \"page\": 2,\n"
			"    \"data\": \"ZZZZ\",\n"
			"    \"data_size\": 4\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 5,\n"
			"    \"page\": 2,\n"
			"    \"addr\": 4,\n"
			"    \"data\": \"ZZ\",\n"
			"    \"data_size\": 2\n"
			"  }\n"
			"]");
}

static void
fu_chunk_array_null_func(void)
{
	g_autofree gchar *chunked1_str = NULL;
	g_autofree gchar *chunked2_str = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;

	chunked1 = fu_chunk_array_new(NULL, 0x100, 0, 0x100, 0x80, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked1);
	g_assert_cmpint(chunked1->len, ==, 2);
	chunked1_str = fu_chunk_array_to_string(chunked1);
	g_assert_cmpstr(chunked1_str,
			==,
			"[\n"
			"  {\n"
			"    \"size\": 128\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 1,\n"
			"    \"addr\": 128,\n"
			"    \"size\": 128\n"
			"  }\n"
			"]");

	chunked2 = fu_chunk_array_new(NULL, 0x200, 0, 0x100, 0x80, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked2);
	g_assert_cmpint(chunked2->len, ==, 4);
	chunked2_str = fu_chunk_array_to_string(chunked2);
	g_assert_cmpstr(chunked2_str,
			==,
			"[\n"
			"  {\n"
			"    \"size\": 128\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 1,\n"
			"    \"addr\": 128,\n"
			"    \"size\": 128\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 2,\n"
			"    \"page\": 1,\n"
			"    \"size\": 128\n"
			"  },\n"
			"  {\n"
			"    \"idx\": 3,\n"
			"    \"page\": 1,\n"
			"    \"addr\": 128,\n"
			"    \"size\": 128\n"
			"  }\n"
			"]");
}

typedef struct {
	guint page;
	gsize address;
	gsize size;
} FuChunkTestExpected;

static void
fu_chunk_test_check(FuChunk *chk, gsize page_sz, const FuChunkTestExpected *expected)
{
	g_assert_cmpint(fu_chunk_get_page(chk), ==, expected->page);
	g_assert_cmpint(fu_chunk_get_address(chk), ==, expected->address);
	g_assert_cmpint(fu_chunk_get_data_sz(chk), ==, expected->size);
	g_assert_cmpint(fu_chunk_get_address(chk) + fu_chunk_get_data_sz(chk), <=, page_sz);
}

static void
fu_chunk_page_boundary_func(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;
	const FuChunkTestExpected expected1[] = {
	    {0, 0, 7},
	    {0, 7, 3},
	    {1, 0, 7},
	    {1, 7, 3},
	};
	const FuChunkTestExpected expected2[] = {
	    {0, 2, 2},
	    {1, 0, 4},
	    {2, 0, 2},
	};

	/* packet size is not a factor of the page size */
	chunked1 = fu_chunk_array_new(NULL, 20, 0x0, 10, 7, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked1);
	g_assert_cmpint(chunked1->len, ==, G_N_ELEMENTS(expected1));
	for (guint i = 0; i < chunked1->len; i++)
		fu_chunk_test_check(g_ptr_array_index(chunked1, i), 10, &expected1[i]);

	/* address offset is not aligned to the page, and page size equals packet size */
	chunked2 = fu_chunk_array_new(NULL, 8, 0x2, 4, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked2);
	g_assert_cmpint(chunked2->len, ==, G_N_ELEMENTS(expected2));
	for (guint i = 0; i < chunked2->len; i++)
		fu_chunk_test_check(g_ptr_array_index(chunked2, i), 4, &expected2[i]);
}

static void
fu_chunk_page_boundary_genesys_func(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) chunked = NULL;
	const FuChunkTestExpected expected[] = {
	    {0, 0xB000, 0x5000},
	    {1, 0x0000, 0x6000},
	};

	/* GL3525 bank 2: unaligned start that crosses the 64kB flash block */
	chunked = fu_chunk_array_new(NULL, 0xB000, 0xB000, 0x10000, 0xFFC0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunked);
	g_assert_cmpint(chunked->len, ==, G_N_ELEMENTS(expected));
	for (guint i = 0; i < chunked->len; i++)
		fu_chunk_test_check(g_ptr_array_index(chunked, i), 0x10000, &expected[i]);
}

static void
fu_chunk_array_page_boundary_func(void)
{
	g_autoptr(FuChunkArray) chunks1 = NULL;
	g_autoptr(FuChunkArray) chunks2 = NULL;
	const FuChunkTestExpected expected1[] = {
	    {0, 0, 7},
	    {0, 7, 3},
	    {1, 0, 7},
	    {1, 7, 3},
	};
	const FuChunkTestExpected expected2[] = {
	    {0, 2, 2},
	    {1, 0, 4},
	    {2, 0, 2},
	};

	/* packet size is not a factor of the page size */
	chunks1 = fu_chunk_array_new_virtual(20, 0x0, 10, 7);
	g_assert_nonnull(chunks1);
	g_assert_cmpint(fu_chunk_array_length(chunks1), ==, G_N_ELEMENTS(expected1));
	for (guint i = 0; i < fu_chunk_array_length(chunks1); i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks1, i, &error);
		g_assert_no_error(error);
		g_assert_nonnull(chk);
		fu_chunk_test_check(chk, 10, &expected1[i]);
	}

	/* address offset is not aligned to the page, and page size equals packet size */
	chunks2 = fu_chunk_array_new_virtual(8, 0x2, 4, 4);
	g_assert_nonnull(chunks2);
	g_assert_cmpint(fu_chunk_array_length(chunks2), ==, G_N_ELEMENTS(expected2));
	for (guint i = 0; i < fu_chunk_array_length(chunks2); i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks2, i, &error);
		g_assert_no_error(error);
		g_assert_nonnull(chk);
		fu_chunk_test_check(chk, 4, &expected2[i]);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/chunk", fu_chunk_func);
	g_test_add_func("/fwupd/chunk-array", fu_chunk_array_func);
	g_test_add_func("/fwupd/chunk-array/null", fu_chunk_array_null_func);
	g_test_add_func("/fwupd/chunk/page-boundary", fu_chunk_page_boundary_func);
	g_test_add_func("/fwupd/chunk/page-boundary/genesys", fu_chunk_page_boundary_genesys_func);
	g_test_add_func("/fwupd/chunk-array/page-boundary", fu_chunk_array_page_boundary_func);
	return g_test_run();
}
