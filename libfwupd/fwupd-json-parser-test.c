/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "fwupd-json-node.h"
#include "fwupd-json-object.h"
#include "fwupd-json-parser.h"

static const gchar *json_data = "{\"Key\":\"value\"}";

static void
fwupd_json_parser_valid_func(void)
{
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonObject) json_obj2 = NULL;
	g_autoptr(FwupdJsonObject) json_obj3 = NULL;
	g_autoptr(FwupdJsonParser) parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) node = NULL;
	g_autoptr(FwupdJsonNode) node2 = NULL;
	g_autoptr(FwupdJsonNode) node3 = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static(json_data, strlen(json_data));
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* apply some limits */
	fwupd_json_parser_set_max_depth(parser, 10);
	fwupd_json_parser_set_max_items(parser, 100);
	fwupd_json_parser_set_max_quoted(parser, 1000);

	/* from a string */
	node =
	    fwupd_json_parser_load_from_data(parser, json_data, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(node);
	json_obj = fwupd_json_node_get_object(node, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj);
	g_assert_cmpstr(fwupd_json_object_get_string(json_obj, "Key", &error), ==, "value");
	g_assert_no_error(error);

	/* from bytes */
	node2 = fwupd_json_parser_load_from_bytes(parser, blob, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(node2);
	json_obj2 = fwupd_json_node_get_object(node2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj2);

	/* from a seekable stream */
	stream = g_memory_input_stream_new_from_data(json_data, strlen(json_data), NULL);
	node3 =
	    fwupd_json_parser_load_from_stream(parser, stream, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(node3);
	json_obj3 = fwupd_json_node_get_object(node3, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj3);
}

static void
fwupd_json_parser_invalid_func(void)
{
	g_autoptr(FwupdJsonParser) parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) node1 = NULL;
	g_autoptr(FwupdJsonNode) node2 = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("{not valid", 10);
	g_autoptr(GError) error1 = NULL;
	g_autoptr(GError) error2 = NULL;

	node1 = fwupd_json_parser_load_from_data(parser,
						 "{not valid",
						 FWUPD_JSON_LOAD_FLAG_NONE,
						 &error1);
	g_assert_nonnull(error1);
	g_assert_null(node1);

	node2 = fwupd_json_parser_load_from_bytes(parser, blob, FWUPD_JSON_LOAD_FLAG_NONE, &error2);
	g_assert_nonnull(error2);
	g_assert_null(node2);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/json-parser/valid", fwupd_json_parser_valid_func);
	g_test_add_func("/fwupd/json-parser/invalid", fwupd_json_parser_invalid_func);
	return g_test_run();
}
