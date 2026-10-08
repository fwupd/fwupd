/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-json-object.h"

static void
fwupd_codec_string_append_func(void)
{
	g_autoptr(GString) str = g_string_new(NULL);

	/* each helper appends "key: value\n" */
	fwupd_codec_string_append(str, 0, "Key", "value");
	fwupd_codec_string_append_int(str, 0, "Int", 123);
	fwupd_codec_string_append_hex(str, 0, "Hex", 0x1234);
	fwupd_codec_string_append_bool(str, 0, "BoolTrue", TRUE);
	fwupd_codec_string_append_bool(str, 0, "BoolFalse", FALSE);
	fwupd_codec_string_append_time(str, 0, "Time", 1600000000);
	fwupd_codec_string_append_size(str, 0, "Size", 1024 * 1024);

	g_assert_nonnull(g_strstr_len(str->str, -1, "Key:"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "value"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "Int:"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "123"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "Hex:"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "0x1234"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "BoolTrue:"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "true"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "false"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "Time:"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "Size:"));

	/* zero values are ignored for int/hex/time/size */
	g_string_truncate(str, 0);
	fwupd_codec_string_append_int(str, 0, "Int", 0);
	fwupd_codec_string_append_hex(str, 0, "Hex", 0);
	fwupd_codec_string_append_time(str, 0, "Time", 0);
	fwupd_codec_string_append_size(str, 0, "Size", 0);
	fwupd_codec_string_append(str, 0, "Null", NULL);
	g_assert_cmpstr(str->str, ==, "");

	/* indented, multiline value */
	g_string_truncate(str, 0);
	fwupd_codec_string_append(str, 1, "Multi", "line1\nline2");
	g_assert_nonnull(g_strstr_len(str->str, -1, "line1"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "line2"));
}

static void
fwupd_codec_json_append_func(void)
{
	g_autoptr(GString) json = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GHashTable) map = g_hash_table_new(g_str_hash, g_str_equal);
	const gchar *strv[] = {"one", "two", NULL};

	g_hash_table_insert(map, (gpointer) "MapKey", (gpointer) "MapValue");

	fwupd_codec_json_append(json_obj, "Key", "value");
	fwupd_codec_json_append_int(json_obj, "Int", 123);
	fwupd_codec_json_append_bool(json_obj, "BoolTrue", TRUE);
	fwupd_codec_json_append_bool(json_obj, "BoolFalse", FALSE);
	fwupd_codec_json_append_strv(json_obj, "Strv", (gchar **)strv);
	fwupd_codec_json_append_map(json_obj, "Map", map);

	/* NULL values are ignored */
	fwupd_codec_json_append_strv(json_obj, "StrvNull", NULL);
	fwupd_codec_json_append_map(json_obj, "MapNull", NULL);

	json = fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_assert_nonnull(json);
	g_assert_nonnull(g_strstr_len(json->str, -1, "Key"));
	g_assert_nonnull(g_strstr_len(json->str, -1, "value"));
	g_assert_nonnull(g_strstr_len(json->str, -1, "Int"));
	g_assert_nonnull(g_strstr_len(json->str, -1, "BoolTrue"));
	g_assert_nonnull(g_strstr_len(json->str, -1, "Strv"));
	g_assert_nonnull(g_strstr_len(json->str, -1, "one"));
	g_assert_nonnull(g_strstr_len(json->str, -1, "MapKey"));
	g_assert_nonnull(g_strstr_len(json->str, -1, "MapValue"));
	g_assert_null(g_strstr_len(json->str, -1, "StrvNull"));
	g_assert_null(g_strstr_len(json->str, -1, "MapNull"));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/codec/string-append", fwupd_codec_string_append_func);
	g_test_add_func("/fwupd/codec/json-append", fwupd_codec_json_append_func);
	return g_test_run();
}
