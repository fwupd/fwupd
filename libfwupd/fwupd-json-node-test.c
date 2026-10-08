/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-json-array.h"
#include "fwupd-json-node.h"
#include "fwupd-json-object.h"

static void
fwupd_json_node_string_func(void)
{
	g_autoptr(FwupdJsonNode) node = fwupd_json_node_new_string("value");
	g_autoptr(FwupdJsonNode) node_ref = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str = NULL;

	g_assert_cmpint(fwupd_json_node_get_kind(node), ==, FWUPD_JSON_NODE_KIND_STRING);

	/* ref and unref */
	node_ref = fwupd_json_node_ref(node);
	g_assert_true(node_ref == node);

	g_assert_cmpstr(fwupd_json_node_get_string(node, &error), ==, "value");
	g_assert_no_error(error);

	str = fwupd_json_node_to_string(node, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_assert_nonnull(str);
	g_assert_nonnull(g_strstr_len(str->str, -1, "value"));

	/* not an object or array */
	g_assert_null(fwupd_json_node_get_object(node, &error));
	g_assert_nonnull(error);
}

static void
fwupd_json_node_raw_func(void)
{
	g_autoptr(FwupdJsonNode) node = fwupd_json_node_new_raw("1234");
	g_autoptr(GError) error = NULL;

	g_assert_cmpint(fwupd_json_node_get_kind(node), ==, FWUPD_JSON_NODE_KIND_RAW);
	g_assert_cmpstr(fwupd_json_node_get_raw(node, &error), ==, "1234");
	g_assert_no_error(error);
}

static void
fwupd_json_node_object_func(void)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonObject) json_obj2 = NULL;
	g_autoptr(FwupdJsonNode) node = NULL;
	g_autoptr(GError) error = NULL;

	fwupd_json_object_add_string(json_obj, "Key", "value");
	node = fwupd_json_node_new_object(json_obj);
	g_assert_cmpint(fwupd_json_node_get_kind(node), ==, FWUPD_JSON_NODE_KIND_OBJECT);

	json_obj2 = fwupd_json_node_get_object(node, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj2);
	g_assert_cmpstr(fwupd_json_object_get_string(json_obj2, "Key", &error), ==, "value");
	g_assert_no_error(error);
}

static void
fwupd_json_node_array_func(void)
{
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
	g_autoptr(FwupdJsonArray) json_arr2 = NULL;
	g_autoptr(FwupdJsonNode) node = NULL;
	g_autoptr(GError) error = NULL;

	fwupd_json_array_add_string(json_arr, "value");
	node = fwupd_json_node_new_array(json_arr);
	g_assert_cmpint(fwupd_json_node_get_kind(node), ==, FWUPD_JSON_NODE_KIND_ARRAY);

	json_arr2 = fwupd_json_node_get_array(node, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_arr2);
	g_assert_cmpint(fwupd_json_array_get_size(json_arr2), ==, 1);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/json-node/string", fwupd_json_node_string_func);
	g_test_add_func("/fwupd/json-node/raw", fwupd_json_node_raw_func);
	g_test_add_func("/fwupd/json-node/object", fwupd_json_node_object_func);
	g_test_add_func("/fwupd/json-node/array", fwupd_json_node_array_func);
	return g_test_run();
}
