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
	    fu_chunk_array_new_from_bytes(fw, 100, FU_CHUNK_PAGESZ_NONE, 5);

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
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;
	g_autoptr(GPtrArray) chunked3 = NULL;
	g_autoptr(GPtrArray) chunked4 = NULL;
	g_autoptr(GPtrArray) chunked5 = NULL;

	chunked3 = fu_chunk_array_new((const guint8 *)"123456", 6, 0x0, 3, 3);
	chunked3_str = fu_chunk_array_to_string(chunked3);
	g_assert_cmpstr(chunked3_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x3\">123</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x3\">456</data>\n"
			"  </chunk>\n"
			"</chunks>\n");

	chunked4 = fu_chunk_array_new((const guint8 *)"123456", 6, 0x4, 4, 4);
	chunked4_str = fu_chunk_array_to_string(chunked4);
	g_assert_cmpstr(chunked4_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">1234</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <page>0x2</page>\n"
			"    <data size=\"0x2\">56</data>\n"
			"  </chunk>\n"
			"</chunks>\n");
	chunked5 = fu_chunk_array_new(NULL, 0, 0x0, 0x0, 4);
	g_assert_cmpint(chunked5->len, ==, 0);
	chunked5_str = fu_chunk_array_to_string(chunked5);
	if (fu_version_compare(xb_version_string(), "0.3.22", FWUPD_VERSION_FORMAT_TRIPLET) >= 0) {
		g_assert_cmpstr(chunked5_str, ==, "<chunks />\n");
	} else {
		g_assert_cmpstr(chunked5_str, ==, "<chunks>\n</chunks>\n");
	}

	chunked1 = fu_chunk_array_new((const guint8 *)"0123456789abcdef", 16, 0x0, 10, 4);
	chunked1_str = fu_chunk_array_to_string(chunked1);
	g_assert_cmpstr(chunked1_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x4\">0123</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x4\">4567</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x2</idx>\n"
			"    <addr>0x8</addr>\n"
			"    <data size=\"0x2\">89</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x3</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">abcd</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x4</idx>\n"
			"    <page>0x1</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">ef</data>\n"
			"  </chunk>\n"
			"</chunks>\n");
	chunked2 = fu_chunk_array_new((const guint8 *)"XXXXXXYYYYYYZZZZZZ", 18, 0x0, 6, 4);
	chunked2_str = fu_chunk_array_to_string(chunked2);
	g_debug("%s", chunked2_str);
	g_assert_cmpstr(chunked2_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x4\">XXXX</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">XX</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x2</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">YYYY</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x3</idx>\n"
			"    <page>0x1</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">YY</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x4</idx>\n"
			"    <page>0x2</page>\n"
			"    <data size=\"0x4\">ZZZZ</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x5</idx>\n"
			"    <page>0x2</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">ZZ</data>\n"
			"  </chunk>\n"
			"</chunks>\n");
}

static void
fu_chunk_array_null_func(void)
{
	g_autofree gchar *chunked1_str = NULL;
	g_autofree gchar *chunked2_str = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;

	chunked1 = fu_chunk_array_new(NULL, 0x100, 0, 0x100, 0x80);
	g_assert_cmpint(chunked1->len, ==, 2);
	chunked1_str = fu_chunk_array_to_string(chunked1);
	g_assert_cmpstr(chunked1_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <size>0x80</size>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <addr>0x80</addr>\n"
			"    <size>0x80</size>\n"
			"  </chunk>\n"
			"</chunks>\n");

	chunked2 = fu_chunk_array_new(NULL, 0x200, 0, 0x100, 0x80);
	g_assert_cmpint(chunked2->len, ==, 4);
	chunked2_str = fu_chunk_array_to_string(chunked2);
	g_assert_cmpstr(chunked2_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <size>0x80</size>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <addr>0x80</addr>\n"
			"    <size>0x80</size>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x2</idx>\n"
			"    <page>0x1</page>\n"
			"    <size>0x80</size>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x3</idx>\n"
			"    <page>0x1</page>\n"
			"    <addr>0x80</addr>\n"
			"    <size>0x80</size>\n"
			"  </chunk>\n"
			"</chunks>\n");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/chunk", fu_chunk_func);
	g_test_add_func("/fwupd/chunk-array", fu_chunk_array_func);
	g_test_add_func("/fwupd/chunk-array/null", fu_chunk_array_null_func);
	return g_test_run();
}
