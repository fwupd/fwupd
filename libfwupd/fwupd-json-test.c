/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-json-array.h"
#include "fwupd-json-object.h"
#include "fwupd-json-parser.h"
#include "fwupd-test.h"

static void
fwupd_json_parser_depth_func(void)
{
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *json = "{\"one\": {\"two\": {\"three\": []}}}";

	fwupd_json_parser_set_max_depth(json_parser, 3);
	fwupd_json_parser_set_max_items(json_parser, 10);
	fwupd_json_parser_set_max_quoted(json_parser, 10);
	json_node =
	    fwupd_json_parser_load_from_data(json_parser, json, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_node);
}

static void
fwupd_json_parser_items_func(void)
{
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *json = "[1,2,3,4]";

	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_quoted(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 3);
	json_node =
	    fwupd_json_parser_load_from_data(json_parser, json, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_node);
}

static void
fwupd_json_parser_quoted_func(void)
{
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *json = "\"hello\"";

	/* set appropriate limits */
	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 100);
	fwupd_json_parser_set_max_quoted(json_parser, 3);
	json_node =
	    fwupd_json_parser_load_from_data(json_parser, json, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_node);
}

static void
fwupd_json_parser_stream_func(void)
{
	const gchar *json = "\"one\"";
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) json_node1 = NULL;
	g_autoptr(FwupdJsonNode) json_node2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new((const guint8 *)json, strlen(json));
	g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_bytes(blob);

	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 10);
	fwupd_json_parser_set_max_quoted(json_parser, 10);
	json_node1 =
	    fwupd_json_parser_load_from_bytes(json_parser, blob, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_node1);
	json_node2 = fwupd_json_parser_load_from_stream(json_parser,
							stream,
							FWUPD_JSON_LOAD_FLAG_NONE,
							&error);
	g_assert_no_error(error);
	g_assert_nonnull(json_node2);
}

static void
fwupd_json_parser_null_func(void)
{
	gboolean ret;
	gint64 value = 0;
	g_autoptr(FwupdJsonNode) json_node2 = NULL;
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str = NULL;

	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 10);
	fwupd_json_parser_set_max_quoted(json_parser, 10);
	json_node = fwupd_json_parser_load_from_data(json_parser,
						     "{\"seven\": null}",
						     FWUPD_JSON_LOAD_FLAG_NONE,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_node);
	json_obj = fwupd_json_node_get_object(json_node, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj);

	/* ensure 'null' is tagged as a string */
	json_node2 = fwupd_json_object_get_node(json_obj, "seven", &error);
	g_assert_cmpint(fwupd_json_node_get_kind(json_node2), ==, FWUPD_JSON_NODE_KIND_NULL);

	/* ensure we use the default integer value */
	ret = fwupd_json_object_get_integer_with_default(json_obj, "seven", &value, 123, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 123);

	str = fwupd_json_node_to_string(json_node2, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_assert_cmpstr(str->str, ==, "null");
}

static void
fwupd_json_parser_valid_func(void)
{
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	const gchar *data[] = {
	    "{\"one\": \"alice\", \"two\": \"bob\"}",
	    "{\"one\": True, \"two\": 123}",
	    "{\"one\": null}",
	    "\"one\"",
	    "\"one\\ttwo\"",
	    "\"two\\nthree\"",
	    "\"four\\\"five\"",
	    "[]",
	    "[\"one\", \"two\\n\", [{\"three\": [true]}]]",
	};

	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 10);
	fwupd_json_parser_set_max_quoted(json_parser, 10);
	for (guint i = 0; i < G_N_ELEMENTS(data); i++) {
		g_autoptr(FwupdJsonNode) json_node = NULL;
		g_autoptr(GError) error = NULL;
		g_autoptr(GString) str = NULL;

		g_debug("IN: %s", data[i]);
		json_node = fwupd_json_parser_load_from_data(json_parser,
							     data[i],
							     FWUPD_JSON_LOAD_FLAG_NONE,
							     &error);
		g_assert_no_error(error);
		g_assert_nonnull(json_node);
		str = fwupd_json_node_to_string(json_node, FWUPD_JSON_EXPORT_FLAG_NONE);
		g_debug("OUT: %s", str->str);
		g_assert_cmpstr(data[i], ==, str->str);
	}
}

static void
fwupd_json_parser_invalid_func(void)
{
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	const gchar *data[] = {
	    "[",
	    "[\"one\": true]",
	    "[\n\"one\":]",
	    "{\"one\", true}",
	    "{one, true}",
	    "\"\\p\"",
	    ":1",
	    "\x02",
	    "\n\n\n\n\n\n\n[]",
	    "         []",
	};

	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 10);
	fwupd_json_parser_set_max_quoted(json_parser, 10);
	for (guint i = 0; i < G_N_ELEMENTS(data); i++) {
		g_autoptr(FwupdJsonNode) json_node = NULL;
		g_autoptr(GError) error = NULL;

		g_debug("IN: %s", data[i]);
		json_node = fwupd_json_parser_load_from_data(json_parser,
							     data[i],
							     FWUPD_JSON_LOAD_FLAG_NONE,
							     &error);
		g_assert_nonnull(error);
		g_assert_cmpint(error->domain, ==, FWUPD_ERROR);
		g_assert_null(json_node);
	}
}

static void
fwupd_json_object_func(void)
{
	const gchar *tmp;
	gboolean ret;
	gint64 rc;
	gboolean tmpb;
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
	g_autoptr(FwupdJsonArray) json_arr_tmp = NULL;
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj2 = fwupd_json_object_new();
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonObject) json_obj_tmp = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error3 = NULL;
	g_autoptr(GError) error4 = NULL;
	g_autoptr(GError) error5 = NULL;
	g_autoptr(GError) error6 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) json_keys = NULL;
	g_autoptr(GPtrArray) json_nodes = NULL;
	g_autoptr(GString) str2 = NULL;
	g_autoptr(GString) str = NULL;

	g_assert_cmpint(fwupd_json_object_get_size(json_obj), ==, 0);

	fwupd_json_object_add_string(json_obj, "one", "alice");
	fwupd_json_object_add_string(json_obj, "one", "bob");
	fwupd_json_object_add_string(json_obj, "two", "clara\ndave");
	fwupd_json_object_add_integer(json_obj, "three", 3);
	fwupd_json_object_add_string(json_obj, "four", "");
	fwupd_json_object_add_boolean(json_obj, "six", TRUE);
	fwupd_json_object_add_string(json_obj, "seven", NULL);
	g_assert_cmpint(fwupd_json_object_get_size(json_obj), ==, 6);

	tmp = fwupd_json_object_get_string(json_obj, "one", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "bob");
	tmp = fwupd_json_object_get_string(json_obj, "two", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "clara\ndave");
	ret = fwupd_json_object_get_integer(json_obj, "three", &rc, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(rc, ==, 3);
	ret = fwupd_json_object_get_boolean(json_obj, "six", &tmpb, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(tmpb);
	g_assert_true(fwupd_json_object_has_node(json_obj, "six"));

	json_nodes = fwupd_json_object_get_nodes(json_obj);
	g_assert_nonnull(json_nodes);
	g_assert_cmpint(json_nodes->len, ==, 6);
	json_keys = fwupd_json_object_get_keys(json_obj);
	g_assert_nonnull(json_keys);
	g_assert_cmpint(json_keys->len, ==, 6);

	tmp = fwupd_json_object_get_string(json_obj, "four", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "");
	tmp = fwupd_json_object_get_string(json_obj, "five", &error3);
	g_assert_error(error3, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(tmp);

	/* get by position */
	tmp = fwupd_json_object_get_key_for_index(json_obj, 0, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "one");
	json_node = fwupd_json_object_get_node_for_index(json_obj, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_node);
	tmp = fwupd_json_node_get_string(json_node, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "bob");

	/* exists, but is unget-able */
	g_assert_true(fwupd_json_object_has_node(json_obj, "seven"));
	tmp = fwupd_json_object_get_string(json_obj, "seven", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_cmpstr(tmp, ==, NULL);
	g_clear_error(&error);

	/* export */
	str2 = fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_INDENT);
	g_debug("%s", str2->str);
	ret = fu_test_compare_lines(str2->str,
				    "{\n"
				    "  \"one\": \"bob\",\n"
				    "  \"two\": \"clara\\ndave\",\n"
				    "  \"three\": 3,\n"
				    "  \"four\": \"\",\n"
				    "  \"six\": true,\n"
				    "  \"seven\": null\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob = fwupd_json_object_to_bytes(json_obj, FWUPD_JSON_EXPORT_FLAG_INDENT);
	g_assert_nonnull(blob);
	g_assert_cmpint(g_bytes_get_size(blob), ==, str2->len);

	/* does not exist */
	json_arr_tmp = fwupd_json_object_get_array(json_obj, "one", &error4);
	g_assert_error(error4, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_arr_tmp);
	json_obj_tmp = fwupd_json_object_get_object(json_obj, "one", &error5);
	g_assert_error(error5, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_obj_tmp);

	/* add array */
	fwupd_json_array_add_string(json_arr, "dave");
	fwupd_json_object_add_array(json_obj, "array", json_arr);

	/* add object */
	fwupd_json_object_add_integer(json_obj2, "int", 123);
	fwupd_json_object_add_object(json_obj, "object", json_obj2);

	/* get unknown with default value */
	ret = fwupd_json_object_get_integer_with_default(json_obj2, "XXX", &rc, 123, &error6);
	g_assert_no_error(error6);
	g_assert_true(ret);
	g_assert_cmpint(rc, ==, 123);
	tmp = fwupd_json_object_get_string_with_default(json_obj, "XXX", "dave", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "dave");
	ret = fwupd_json_object_get_boolean_with_default(json_obj, "XXX", &tmpb, TRUE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(tmpb);

	str = fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_INDENT);
	g_debug("%s", str->str);
	ret = fu_test_compare_lines(str->str,
				    "{\n"
				    "  \"one\": \"bob\",\n"
				    "  \"two\": \"clara\\ndave\",\n"
				    "  \"three\": 3,\n"
				    "  \"four\": \"\",\n"
				    "  \"six\": true,\n"
				    "  \"seven\": null,\n"
				    "  \"array\": [\n"
				    "    \"dave\"\n"
				    "  ],\n"
				    "  \"object\": {\n"
				    "    \"int\": 123\n"
				    "  }\n"
				    "}",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fwupd_json_node_func(void)
{
	const gchar *tmp;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str = NULL;
	g_autoptr(FwupdJsonNode) json_node = fwupd_json_node_new_raw("dave");
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonArray) json_arr = NULL;

	tmp = fwupd_json_node_get_raw(json_node, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "dave");
	str = fwupd_json_node_to_string(json_node, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_assert_cmpstr(str->str, ==, "dave");

	/* get the wrong type */
	tmp = fwupd_json_node_get_string(json_node, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_cmpstr(tmp, ==, NULL);
	g_clear_error(&error);
	json_obj = fwupd_json_node_get_object(json_node, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_obj);
	g_clear_error(&error);
	json_arr = fwupd_json_node_get_array(json_node, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_arr);
}

static void
fwupd_json_array_func(void)
{
	const gchar *tmp;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str = NULL;
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
	g_autoptr(FwupdJsonArray) json_arr_tmp = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	g_assert_cmpint(fwupd_json_array_get_size(json_arr), ==, 0);

	fwupd_json_array_add_string(json_arr, "hello");
	fwupd_json_array_add_raw(json_arr, "world");
	g_assert_cmpint(fwupd_json_array_get_size(json_arr), ==, 2);

	tmp = fwupd_json_array_get_string(json_arr, 0, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "hello");
	tmp = fwupd_json_array_get_raw(json_arr, 1, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "world");
	tmp = fwupd_json_array_get_string(json_arr, 2, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(tmp);
	g_clear_error(&error);

	/* wrong type */
	tmp = fwupd_json_array_get_raw(json_arr, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(tmp);
	g_clear_error(&error);
	json_obj = fwupd_json_array_get_object(json_arr, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_obj);
	g_clear_error(&error);
	json_arr_tmp = fwupd_json_array_get_array(json_arr, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(json_arr_tmp);
	g_clear_error(&error);

	str = fwupd_json_array_to_string(json_arr, FWUPD_JSON_EXPORT_FLAG_INDENT);
	g_debug("%s", str->str);
	g_assert_cmpstr(str->str, ==, "[\n  \"hello\",\n  world\n]");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/json/node", fwupd_json_node_func);
	g_test_add_func("/fwupd/json/array", fwupd_json_array_func);
	g_test_add_func("/fwupd/json/object", fwupd_json_object_func);
	g_test_add_func("/fwupd/json/parser/valid", fwupd_json_parser_valid_func);
	g_test_add_func("/fwupd/json/parser/invalid", fwupd_json_parser_invalid_func);
	g_test_add_func("/fwupd/json/parser/null", fwupd_json_parser_null_func);
	g_test_add_func("/fwupd/json/parser/depth", fwupd_json_parser_depth_func);
	g_test_add_func("/fwupd/json/parser/items", fwupd_json_parser_items_func);
	g_test_add_func("/fwupd/json/parser/quoted", fwupd_json_parser_quoted_func);
	g_test_add_func("/fwupd/json/parser/stream", fwupd_json_parser_stream_func);
	return g_test_run();
}
