/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_string_utf16_func(void)
{
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error = NULL;

	buf = fu_utf8_to_utf16_byte_array("hello world",
					  G_LITTLE_ENDIAN,
					  FU_UTF_CONVERT_FLAG_APPEND_NUL,
					  &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf);
	g_assert_cmpint(buf->len, ==, 24);
	g_assert_cmpint(buf->data[0], ==, 'h');
	g_assert_cmpint(buf->data[1], ==, '\0');
	g_assert_cmpint(buf->data[2], ==, 'e');
	g_assert_cmpint(buf->data[3], ==, '\0');
	str1 = fu_utf16_to_utf8_byte_array(buf, G_LITTLE_ENDIAN, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str1, ==, "hello world");

	/* failure */
	g_byte_array_set_size(buf, buf->len - 1);
	str2 = fu_utf16_to_utf8_byte_array(buf, G_LITTLE_ENDIAN, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_cmpstr(str2, ==, NULL);
}

static void
fu_strpassmask_func(void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {{"foo https://test.com/auth bar", "foo https://test.com/auth bar"},
		    {"foo https://user%40host:SECRET@test.com/auth bar",
		     "foo https://user%40host:XXXXXX@test.com/auth bar"},
		    {"foo https://user1%40host:SECRET@test.com/auth "
		     "https://user2%40host:SECRET2@test.com/auth bar",
		     "foo https://user1%40host:XXXXXX@test.com/auth "
		     "https://user2%40host:XXXXXXX@test.com/auth bar"},
		    {NULL, NULL}};
	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_strpassmask(strs[i].in);
		g_assert_cmpstr(tmp, ==, strs[i].op);
	}
}

static gboolean
fu_string_strnsplit_add_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	GPtrArray *array = (GPtrArray *)user_data;
	g_debug("TOKEN: [%s] (%u)", token->str, token_idx);
	g_ptr_array_add(array, g_strdup(token->str));
	return TRUE;
}

static gboolean
fu_string_strnsplit_nop_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
	return TRUE;
}

static void
fu_strsplit_func(void)
{
	const gchar *str = "123foo123bar123";
	const guint bigsz = 1024 * 1024;
	gboolean ret;
	guint cnt = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GString) bigstr = g_string_sized_new(bigsz * 2);

	/* works for me */
	ret = fu_strsplit_full(str, -1, "123", fu_string_strnsplit_add_cb, array, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(array->len, ==, 4);
	g_assert_cmpstr(g_ptr_array_index(array, 0), ==, "");
	g_assert_cmpstr(g_ptr_array_index(array, 1), ==, "foo");
	g_assert_cmpstr(g_ptr_array_index(array, 2), ==, "bar");
	g_assert_cmpstr(g_ptr_array_index(array, 3), ==, "");

	/* lets try something insane */
	for (guint i = 0; i < bigsz; i++)
		g_string_append(bigstr, "X\n");
	ret = fu_strsplit_full(bigstr->str, -1, "\n", fu_string_strnsplit_nop_cb, &cnt, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	/* we have an empty last section */
	g_assert_cmpint(cnt, ==, bigsz + 1);
}

static void
fu_strstrip_func(void)
{
	struct {
		const gchar *old;
		const gchar *new;
	} map[] = {{"same", "same"},
		   {" leading", "leading"},
		   {"tailing ", "tailing"},
		   {"  b  ", "b"},
		   {"  ", ""},
		   {NULL, NULL}};
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autofree gchar *tmp = fu_strstrip(map[i].old);
		g_assert_cmpstr(tmp, ==, map[i].new);
	}
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autoptr(GString) str = g_string_new(map[i].old);
		fu_string_strip(str);
		g_assert_cmpstr(str->str, ==, map[i].new);
	}
}

static void
fu_strtoull_func(void)
{
	gboolean ret;
	guint64 val = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_strtoull("123", &val, 123, 200, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 123);

	ret = fu_strtoull("123\n", &val, 0, 200, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 123);

	ret = fu_strtoull("0x123", &val, 0, 0x123, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 0x123);

	ret = fu_strtoull(NULL, &val, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("124", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("119", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
}

static void
fu_strtoll_func(void)
{
	gboolean ret;
	gint64 val = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_strtoll("123", &val, 123, 200, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 123);

	ret = fu_strtoll("-123\n", &val, -123, 200, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, -123);

	ret = fu_strtoll("0x123", &val, 0, 0x123, FU_INTEGER_BASE_AUTO, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 0x123);

	ret = fu_strtoll(NULL, &val, 0, G_MAXINT32, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoll("", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoll("124", &val, 120, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
	ret = fu_strtoll("-124", &val, -123, 123, FU_INTEGER_BASE_AUTO, NULL);
	g_assert_false(ret);
}

static gboolean
fu_strsplit_stream_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	guint *cnt = (guint *)user_data;
	g_debug(">%s<", token->str);
	(*cnt)++;
	return TRUE;
}

static void
fu_strsplit_stream_func(void)
{
	gboolean ret;
	guint cnt1 = 0;
	guint cnt2 = 0;
	guint cnt3 = 0;
	const gchar str1[] = "simple string";
	const gchar str2[] = "123delimited123start123and123end123";
	const gchar str3[] = "this|has|trailing|nuls\0\0\0\0";
	g_autoptr(GInputStream) stream1 = NULL;
	g_autoptr(GInputStream) stream2 = NULL;
	g_autoptr(GInputStream) stream3 = NULL;
	g_autoptr(GError) error = NULL;

	/* check includes NUL */
	g_assert_cmpint(sizeof(str1), ==, 14);

	stream1 = G_INPUT_STREAM(g_memory_input_stream_new_from_data(str1, strlen(str1), NULL));
	ret = fu_strsplit_stream(stream1, 0x0, " ", fu_strsplit_stream_cb, &cnt1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt1, ==, 2);

	stream2 = G_INPUT_STREAM(g_memory_input_stream_new_from_data(str2, strlen(str2), NULL));
	ret = fu_strsplit_stream(stream2, 0x0, "123", fu_strsplit_stream_cb, &cnt2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt2, ==, 6);

	stream3 = G_INPUT_STREAM(g_memory_input_stream_new_from_data(str3, sizeof(str3), NULL));
	ret = fu_strsplit_stream(stream3, 0x0, "|", fu_strsplit_stream_cb, &cnt3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt3, ==, 4);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/string/utf16", fu_string_utf16_func);
	g_test_add_func("/fwupd/string/password-mask", fu_strpassmask_func);
	g_test_add_func("/fwupd/string/strsplit-stream", fu_strsplit_stream_func);
	g_test_add_func("/fwupd/string/strtoull", fu_strtoull_func);
	g_test_add_func("/fwupd/string/strtoll", fu_strtoll_func);
	g_test_add_func("/fwupd/string/strstrip", fu_strstrip_func);
	g_test_add_func("/fwupd/string/strnsplit", fu_strsplit_func);
	return g_test_run();
}
