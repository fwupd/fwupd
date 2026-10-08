/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-json-array.h"
#include "fwupd-json-node.h"
#include "fwupd-json-object.h"

static void
fwupd_json_array_func(void)
{
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
	g_autoptr(FwupdJsonArray) json_arr_ref = NULL;
	g_autoptr(FwupdJsonArray) json_arr_nested = fwupd_json_array_new();
	g_autoptr(FwupdJsonArray) json_arr2 = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonObject) json_obj2 = NULL;
	g_autoptr(FwupdJsonNode) json_node = fwupd_json_node_new_string("noded");
	g_autoptr(FwupdJsonNode) json_node2 = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("AB", 2);
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str = NULL;

	/* empty to start with */
	g_assert_cmpint(fwupd_json_array_get_size(json_arr), ==, 0);

	/* ref and unref */
	json_arr_ref = fwupd_json_array_ref(json_arr);
	g_assert_true(json_arr_ref == json_arr);

	/* add each kind of member */
	fwupd_json_object_add_string(json_obj, "Key", "value");
	fwupd_json_array_add_string(json_arr_nested, "nestedval");
	fwupd_json_array_add_string(json_arr, "hello");
	fwupd_json_array_add_raw(json_arr, "world");
	fwupd_json_array_add_object(json_arr, json_obj);
	fwupd_json_array_add_array(json_arr, json_arr_nested);
	fwupd_json_array_add_bytes(json_arr, blob);
	fwupd_json_array_add_node(json_arr, json_node);
	g_assert_cmpint(fwupd_json_array_get_size(json_arr), ==, 6);

	/* read back the string member */
	g_assert_cmpstr(fwupd_json_array_get_string(json_arr, 0, &error), ==, "hello");
	g_assert_no_error(error);

	/* the raw member */
	g_assert_nonnull(fwupd_json_array_get_raw(json_arr, 1, &error));
	g_assert_no_error(error);

	/* read back the object member */
	json_obj2 = fwupd_json_array_get_object(json_arr, 2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj2);
	g_assert_cmpstr(fwupd_json_object_get_string(json_obj2, "Key", &error), ==, "value");
	g_assert_no_error(error);

	/* read back the nested array member */
	json_arr2 = fwupd_json_array_get_array(json_arr, 3, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_arr2);
	g_assert_cmpint(fwupd_json_array_get_size(json_arr2), ==, 1);

	/* a generic node */
	json_node2 = fwupd_json_array_get_node(json_arr, 0, &error);
	g_assert_nonnull(json_node2);
	g_assert_no_error(error);

	/* serialize */
	str = fwupd_json_array_to_string(json_arr, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_assert_nonnull(str);
	g_assert_nonnull(g_strstr_len(str->str, -1, "hello"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "value"));
	g_assert_nonnull(g_strstr_len(str->str, -1, "nestedval"));
}

static void
fwupd_json_array_invalid_func(void)
{
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(GError) error = NULL;

	/* reading past the end is an error */
	g_assert_null(fwupd_json_array_get_string(json_arr, 99, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_clear_error(&error);
	g_assert_null(fwupd_json_array_get_raw(json_arr, 99, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_clear_error(&error);
	json_node = fwupd_json_array_get_node(json_arr, 99, &error);
	g_assert_null(json_node);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/json-array", fwupd_json_array_func);
	g_test_add_func("/fwupd/json-array/invalid", fwupd_json_array_invalid_func);
	return g_test_run();
}
