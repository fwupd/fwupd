/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_protobuf_uint64_func(void)
{
	gboolean ret;
	guint64 value = 0;
	gboolean valueb = 0;
	g_autofree gchar *bufst = NULL;
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_autoptr(GError) error = NULL;

	/* 150 -> 10010110 00000001 9601 */
	fu_protobuf_add_uint64(pbuf, 1, 150);
	fu_protobuf_add_boolean(pbuf, 2, TRUE);
	bufst = fu_protobuf_to_string(pbuf);
	g_assert_cmpstr(bufst, ==, "0896011001");

	/* parse back to varint */
	ret = fu_protobuf_get_uint64(pbuf, 1, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 150);

	/* force looking for not the first field number */
	ret = fu_protobuf_get_uint64(pbuf, 2, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 1);

	/* boolean */
	ret = fu_protobuf_get_boolean(pbuf, 2, &valueb, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(valueb);
}

static void
fu_protobuf_string_func(void)
{
	g_autofree gchar *bufstr = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_autoptr(GError) error = NULL;

	fu_protobuf_add_string(pbuf, 2, "testing");
	bufstr = fu_protobuf_to_string(pbuf);
	g_assert_cmpstr(bufstr, ==, "120774657374696e67");

	str = fu_protobuf_get_string(pbuf, 2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(str);
	g_assert_cmpstr(str, ==, "testing");
}

static void
fu_protobuf_parse_invalid_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_autoptr(GError) error = NULL;

	/* set varint */
	fu_protobuf_add_boolean(pbuf, 1, TRUE);

	/* get len */
	str = fu_protobuf_get_string(pbuf, 1, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(str);
}

static void
fu_protobuf_parse_big_bool_func(void)
{
	gboolean ret;
	gboolean value = FALSE;
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_autoptr(GError) error = NULL;

	/* set varint that is too big for a boolean */
	fu_protobuf_add_uint64(pbuf, 1, 99);
	ret = fu_protobuf_get_boolean(pbuf, 1, &value, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_protobuf_fuzzing_func(void)
{
	gboolean ret;
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_fuzzer_build_example(FU_FUZZER(pbuf), NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ret = fu_fuzzer_test_input(FU_FUZZER(pbuf), blob, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_protobuf_overflow_func(void)
{
	gboolean ret;
	guint64 value = 0;
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_autoptr(GError) error = NULL;

	fu_protobuf_add_uint64(pbuf, 1, G_MAXUINT64);
	ret = fu_protobuf_get_uint64(pbuf, 1, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, G_MAXUINT64);
}

static void
fu_protobuf_parse_missing_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_autoptr(GError) error = NULL;

	/* set varint */
	fu_protobuf_add_boolean(pbuf, 1, TRUE);

	/* get len */
	str = fu_protobuf_get_string(pbuf, 9, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(str);
}

static void
fu_protobuf_embedded_func(void)
{
	g_autofree gchar *bufstr = NULL;
	g_autofree gchar *bufstr3 = NULL;
	g_autoptr(FuProtobuf) pbuf = fu_protobuf_new();
	g_autoptr(FuProtobuf) pbuf2 = fu_protobuf_new();
	g_autoptr(FuProtobuf) pbuf3 = NULL;
	g_autoptr(GError) error = NULL;

	fu_protobuf_add_string(pbuf2, 2, "bar");
	fu_protobuf_add_embedded(pbuf, 1, pbuf2);
	bufstr = fu_protobuf_to_string(pbuf);
	g_assert_cmpstr(bufstr, ==, "0a051203626172");

	pbuf3 = fu_protobuf_get_embedded(pbuf, 1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(pbuf3);
	bufstr3 = fu_protobuf_to_string(pbuf3);
	g_assert_cmpstr(bufstr3, ==, "1203626172");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/protobuf/uint64", fu_protobuf_uint64_func);
	g_test_add_func("/fwupd/protobuf/string", fu_protobuf_string_func);
	g_test_add_func("/fwupd/protobuf/embedded", fu_protobuf_embedded_func);
	g_test_add_func("/fwupd/protobuf/parse-invalid", fu_protobuf_parse_invalid_func);
	g_test_add_func("/fwupd/protobuf/parse-big-bool", fu_protobuf_parse_big_bool_func);
	g_test_add_func("/fwupd/protobuf/parse-missing", fu_protobuf_parse_missing_func);
	g_test_add_func("/fwupd/protobuf/fuzzing", fu_protobuf_fuzzing_func);
	g_test_add_func("/fwupd/protobuf/overflow", fu_protobuf_overflow_func);
	return g_test_run();
}
